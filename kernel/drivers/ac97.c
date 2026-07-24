#include <kernel/ac97.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/irq.h>
#include <kernel/pic.h>
#include <kernel/idt.h>
#include <kernel/vga.h>

static inline void outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl_(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t  inb(uint16_t p){ uint8_t  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p){ uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }

#define AC97_VENDOR 0x8086
#define AC97_DEVICE 0x2415

/* NAM (mixer) register offsets */
#define NAM_RESET       0x00
#define NAM_MASTER_VOL  0x02
#define NAM_PCM_VOL     0x18

/* NABM (bus master) register offsets — PCM OUT box */
#define NABM_PO_BDBAR   0x10 /* buffer descriptor base address, u32 */
#define NABM_PO_CIV     0x14 /* current index value, u8 */
#define NABM_PO_LVI     0x15 /* last valid index, u8 */
#define NABM_PO_SR      0x16 /* status, u16 */
#define NABM_PO_CR      0x1B /* control, u8 */
#define NABM_GLOB_CNT   0x2C /* global control, u32 */

#define CR_RPBM  0x01 /* run/pause bus master */
#define CR_IOCE  0x10 /* interrupt on completion enable */
#define SR_BCIS  0x08 /* buffer completion interrupt status */

static uint16_t nam_base = 0, nabm_base = 0;

/* One BDL (buffer descriptor list) entry — 8 bytes, hardware format. */
typedef struct __attribute__((packed)) {
    uint32_t ptr;
    uint16_t samples; /* length in SAMPLES (not bytes), 16-bit words */
    uint16_t ctrl;
} ac97_bdl_entry_t;

#define BDL_CTRL_IOC 0x8000 /* interrupt on completion for this entry */

static uint64_t bdl_phys = 0;
volatile int last_playback_done = 0;

static void ac97_irq(registers_t* regs) {
    (void)regs;
    uint16_t sr = inw(nabm_base + NABM_PO_SR);
    if (sr & SR_BCIS) last_playback_done = 1;
    outw(nabm_base + NABM_PO_SR, sr); /* ack by writing back what was read */
}

int ac97_init(void) {
    pci_device_t dev;
    if (!pci_find_by_id(AC97_VENDOR, AC97_DEVICE, &dev)) return 0;
    pci_enable_device(&dev);

    nam_base  = (uint16_t)pci_bar_addr(&dev, 0);
    nabm_base = (uint16_t)pci_bar_addr(&dev, 1);
    if (!nam_base || !nabm_base) return 0;

    /* Cold reset via GLOB_CNT bit 1, then wait briefly for the codec
     * to come back — real hardware needs this settle time; a fixed
     * spin is adequate for a QEMU-emulated codec. */
    outl_(nabm_base + NABM_GLOB_CNT, 0x00000002u);
    for (volatile int i = 0; i < 100000; i++) { }

    outw(nam_base + NAM_RESET, 0); /* mixer reset (any write triggers it) */

    /* Unmute, moderate volume (0 = loudest, 0x1F = quietest per
     * channel nibble on real AC97; use a mid-low attenuation rather
     * than max volume). */
    outw(nam_base + NAM_MASTER_VOL, 0x0808);
    outw(nam_base + NAM_PCM_VOL, 0x0808);

    bdl_phys = pmm_alloc_pages(1); /* room for 32 8-byte entries, only need a few */
    if (!bdl_phys) return 0;

    outl_(nabm_base + NABM_PO_BDBAR, (uint32_t)bdl_phys);

    uint32_t irq_word = pci_config_read32(dev.bus, dev.dev, dev.func, 0x3C);
    uint8_t irq_line = (uint8_t)(irq_word & 0xFF);
    if (irq_line < 16) {
        irq_set_handler(irq_line, ac97_irq);
        pic_unmask(irq_line);
        /* Same real gap RTL8139 hit — a slave-PIC line (IRQ 8-15)
         * needs the cascade line (IRQ 2) unmasked too, or nothing on
         * 8-15 can reach the CPU regardless of its own mask bit. */
        if (irq_line >= 8) pic_unmask(2);
    }

    vga_puts_color("  [OK] AC97 audio initialized\n", VGA_LIGHT_GREEN, VGA_BLACK);
    return 1;
}

int ac97_play_pcm(const int16_t* samples, uint32_t sample_count,
                   uint32_t sample_rate, uint8_t channels) {
    (void)sample_rate; /* QEMU's AC97 model plays at a fixed internal
                           rate regardless of what's requested here;
                           real hardware would need the mixer rate
                           registers set — not done in this stage,
                           since QEMU doesn't require it to work. */
    if (!nabm_base || !bdl_phys) return -1;

    /* Copy samples into a physically-contiguous DMA buffer — caller's
     * buffer may not be, and the NIC-driver pattern from stage 5's
     * network work makes clear why that matters for real DMA. Single
     * buffer, up to one page's worth of samples per call (2048
     * int16_t samples = 4KB) — a real player would chunk larger audio
     * across multiple BDL entries; this stage proves playback works
     * at all, not sustained long-form playback. */
    uint32_t max_samples = 4096 / sizeof(int16_t);
    if (sample_count > max_samples) sample_count = max_samples;

    uint64_t audio_buf_phys = pmm_alloc_pages(1);
    if (!audio_buf_phys) return -1;
    int16_t* buf = (int16_t*)audio_buf_phys;
    for (uint32_t i = 0; i < sample_count; i++) buf[i] = samples[i];

    ac97_bdl_entry_t* bdl = (ac97_bdl_entry_t*)bdl_phys;
    bdl[0].ptr = (uint32_t)audio_buf_phys;
    bdl[0].samples = (uint16_t)(sample_count / channels * channels); /* keep frame-aligned */
    bdl[0].ctrl = BDL_CTRL_IOC;

    last_playback_done = 0;
    outb(nabm_base + NABM_PO_LVI, 0); /* one valid entry: index 0 */
    outb(nabm_base + NABM_PO_CR, CR_RPBM | CR_IOCE);

    return 0;
}
