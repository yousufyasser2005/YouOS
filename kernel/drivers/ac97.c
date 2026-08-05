#include <kernel/ac97.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>
#include <kernel/irq.h>
#include <kernel/pic.h>
#include <kernel/idt.h>
#include <kernel/vga.h>
#include <kernel/vmm.h>

extern address_space_t kernel_as;
extern uint64_t irq_get_ticks(void);

static inline void outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl_(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t  inb(uint16_t p){ uint8_t  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p){ uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }

#define AC97_VENDOR 0x8086
#define AC97_DEVICE 0x2415

#define NAM_RESET       0x00
#define NAM_MASTER_VOL  0x02
#define NAM_PCM_VOL     0x18

#define NABM_PO_BDBAR   0x10
#define NABM_PO_CIV     0x14
#define NABM_PO_LVI     0x15
#define NABM_PO_SR      0x16
#define NABM_PO_CR      0x1B
#define NABM_GLOB_CNT   0x2C

#define CR_RPBM  0x01
#define CR_RR    0x02
#define CR_IOCE  0x10
#define SR_DCH   0x01
#define SR_BCIS  0x08

static uint16_t nam_base = 0, nabm_base = 0;

typedef struct __attribute__((packed)) {
    uint32_t ptr;
    uint16_t samples;
    uint16_t ctrl;
} ac97_bdl_entry_t;

#define BDL_CTRL_IOC 0x8000
#define AC97_PAGE_SIZE 4096
#define PAGE_SAMPLES (AC97_PAGE_SIZE / (int)sizeof(int16_t))
#define MAX_WAV_SAMPLES 122880 /* 60 pages = 240KB, enough for ding.wav */

/* Statically allocate ALL DMA memory in .bss to bypass PMM entirely. */
static ac97_bdl_entry_t k_bdl[1] __attribute__((aligned(4096)));
static int16_t k_ring_buf[PAGE_SAMPLES] __attribute__((aligned(4096)));
static int16_t k_wav_buf[MAX_WAV_SAMPLES] __attribute__((aligned(4096)));

static uint64_t bdl_phys = 0;
static uint64_t ring_phys = 0;

static uint32_t stream_total_samples = 0;
static uint32_t stream_pos = 0;
static uint8_t  stream_channels = 1;
static volatile int stream_active = 0;
static uint64_t stream_last_submit_tick = 0;

static inline uint64_t ac97_irq_save_disable(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static inline void ac97_irq_restore(uint64_t flags) {
    __asm__ volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
}

/* This function is called from three unsynchronized contexts: directly
 * from ac97_stream_start() (syscall), from ac97_stream_tick() (timer
 * IRQ, via irq_common_handler -> scheduler_tick -> do_switch, which can
 * abandon this call frame mid-execution on a suspended stack and let
 * another call to this function run before this one resumes), and from
 * ac97_irq() (AC97 completion IRQ). None of those call sites serialize
 * against each other, and this function does non-atomic multi-step
 * hardware register programming plus shared-buffer writes. Without a
 * lock, a second invocation can interleave mid-sequence and corrupt
 * the very state (k_ring_buf, k_bdl, BDBAR/LVI/CR) the first one is
 * still setting up. cli/popf around the whole body makes each call
 * atomic with respect to every other call, regardless of which context
 * it came from. */
static void ac97_stream_feed(void) {
    if (!stream_active) return;

    uint64_t saved_flags = ac97_irq_save_disable();

    /* Wait for previous chunk to finish playing (SR_DCH set) */
    uint16_t sr = inw(nabm_base + NABM_PO_SR);
    if (!(sr & SR_DCH)) {
        ac97_irq_restore(saved_flags);
        return; /* Still playing */
    }
    
    /* Pacing floor removed: §2 diagnostic evidence showed SR_DCH sets
     * reliably and fast after real drain, so gating on SR_DCH alone
     * (checked above) is sufficient. A fixed extra wait on top of that
     * was inserting large silent gaps between short real audio bursts,
     * which produced exactly the "fragments with silence between them"
     * symptom heard during testing. */
    stream_last_submit_tick = irq_get_ticks();
    
    if (stream_pos >= stream_total_samples) {
        stream_active = 0;
        ac97_irq_restore(saved_flags);
        return;
    }
    
    /* Submit exactly 1 page */
    uint32_t want = stream_total_samples - stream_pos;
    if (want > PAGE_SAMPLES) want = PAGE_SAMPLES;
    
    int16_t* dst = k_ring_buf;
    for (uint32_t k = 0; k < want; k++) {
        dst[k] = k_wav_buf[stream_pos + k];
    }
    stream_pos += want;
    
    ac97_bdl_entry_t* bdl = k_bdl;
    bdl[0].ptr = (uint32_t)ring_phys;
    bdl[0].samples = want; /* QEMU expects raw 16-bit sample count */
    bdl[0].ctrl = BDL_CTRL_IOC;
    
    /* Always do a full cold restart for this chunk */
    outb(nabm_base + NABM_PO_CR, 0);
    outw(nabm_base + NABM_PO_SR, 0x1F); /* clear SR */
    outb(nabm_base + NABM_PO_CR, CR_RR);
    for (volatile int i = 0; i < 10000; i++) {
        if (!(inb(nabm_base + NABM_PO_CR) & CR_RR)) break;
    }
    
    outl_(nabm_base + NABM_PO_BDBAR, (uint32_t)bdl_phys);
    outb(nabm_base + NABM_PO_LVI, 0);
    outb(nabm_base + NABM_PO_CR, CR_RPBM | CR_IOCE);

    ac97_irq_restore(saved_flags);
}

int ac97_stream_start(const int16_t* samples, uint32_t total_samples,
                       uint32_t sample_rate, uint8_t channels) {
    (void)sample_rate;
    if (!nabm_base || !bdl_phys) return -1;
    if (channels < 1) channels = 1;
    if (total_samples == 0) return -1;
    if (total_samples > MAX_WAV_SAMPLES) return -1;

    stream_active = 0;
    outb(nabm_base + NABM_PO_CR, 0);
    outb(nabm_base + NABM_PO_CR, CR_RR);
    for (volatile int i = 0; i < 10000; i++) {
        if (!(inb(nabm_base + NABM_PO_CR) & CR_RR)) break;
    }
    
    for (uint32_t i = 0; i < total_samples; i++) {
        k_wav_buf[i] = samples[i];
    }
    stream_total_samples = total_samples;
    stream_pos = 0;
    stream_channels = channels;
    stream_active = 1;
    stream_last_submit_tick = 0;
    
    ac97_stream_feed();
    return 0;
}

int ac97_stream_is_playing(void) { return stream_active; }
void ac97_stream_tick(void) { ac97_stream_feed(); }

static void ac97_irq(registers_t* regs) {
    (void)regs;
    uint16_t sr = inw(nabm_base + NABM_PO_SR);
    outw(nabm_base + NABM_PO_SR, sr);
    if (stream_active) ac97_stream_feed();
}

uint32_t ac97_debug_irq_fire_count(void) { return 0; }
uint32_t ac97_debug_irq_bcis_count(void) { return 0; }
uint32_t ac97_debug_last_sr(void) { return 0; }
uint32_t ac97_debug_last_civ(void) { return 0; }
uint32_t ac97_debug_current_civ_lvi(void) {
    uint8_t civ = inb(nabm_base + NABM_PO_CIV);
    uint8_t lvi = inb(nabm_base + NABM_PO_LVI);
    uint16_t sr = inw(nabm_base + NABM_PO_SR);
    return ((uint32_t)civ << 24) | ((uint32_t)lvi << 16) | sr;
}
uint32_t ac97_debug_ring_counts(void) { return stream_pos; }
uint32_t ac97_debug_path_counts(void) { return stream_total_samples; }
uint32_t ac97_debug_cold_start_duration(void) { return 0; }
uint32_t ac97_debug_last_alloc_fail_pages(void) { return 0; }
void ac97_debug_restart_log_reset(void) { }
uint32_t ac97_debug_restart_log_get(uint32_t idx) { (void)idx; return 0; }
uint32_t ac97_debug_feed_counts_a(void) { return 0; }
uint32_t ac97_debug_feed_counts_b(void) { return 0; }

int ac97_init(void) {
    pci_device_t dev;
    if (!pci_find_by_id(AC97_VENDOR, AC97_DEVICE, &dev)) return 0;
    pci_enable_device(&dev);

    nam_base  = (uint16_t)pci_bar_addr(&dev, 0);
    nabm_base = (uint16_t)pci_bar_addr(&dev, 1);
    if (!nam_base || !nabm_base) return 0;

    outl_(nabm_base + NABM_GLOB_CNT, 0x00000002u);
    for (volatile int i = 0; i < 100000; i++) { }

    outw(nam_base + NAM_RESET, 0);
    outw(nam_base + NAM_MASTER_VOL, 0x0808);
    outw(nam_base + NAM_PCM_VOL, 0x0808);

    /* Get physical addresses of statically allocated buffers at boot.
     * This is 100% safe from PMM overlap and VMM translation bugs. */
    bdl_phys = vmm_get_phys(&kernel_as, (uint64_t)k_bdl);
    if (!bdl_phys) {
        vga_puts("  [ERR] AC97: Failed to get phys for BDL\n");
        return 0;
    }
    ring_phys = vmm_get_phys(&kernel_as, (uint64_t)k_ring_buf);
    if (!ring_phys) return 0;

    stream_active = 0;
    outl_(nabm_base + NABM_PO_BDBAR, (uint32_t)bdl_phys);

    uint32_t irq_word = pci_config_read32(dev.bus, dev.dev, dev.func, 0x3C);
    uint8_t irq_line = (uint8_t)(irq_word & 0xFF);
    if (irq_line < 16) {
        irq_set_handler(irq_line, ac97_irq);
        pic_unmask(irq_line);
        if (irq_line >= 8) pic_unmask(2);
    }

    vga_puts_color("  [OK] AC97 audio initialized\n", VGA_LIGHT_GREEN, VGA_BLACK);
    return 1;
}

int ac97_play_pcm(const int16_t* samples, uint32_t sample_count,
                   uint32_t sample_rate, uint8_t channels) {
    (void)sample_rate;
    (void)samples;
    (void)sample_count;
    (void)channels;
    return -1;
}

int ac97_is_done(void) {
    return !stream_active;
}

int ac97_can_submit(void) {
    return 1;
}
