#include <kernel/gdt.h>

#define GDT_PRESENT    (1 << 7)
#define GDT_RING0      (0 << 5)
#define GDT_RING3      (3 << 5)
#define GDT_CODE_DATA  (1 << 4)
#define GDT_EXECUTABLE (1 << 3)
#define GDT_WRITABLE   (1 << 1)
#define GDT_READABLE   (1 << 1)
#define GDT_GRAN_4K    (1 << 7)
#define GDT_LONG_MODE  (1 << 5)
#define GDT_32BIT      (1 << 6)
#define GDT_TSS_TYPE   0x09

/* Full GDT layout in one struct so it's contiguous in memory */
typedef struct {
    gdt_entry_t     null;
    gdt_entry_t     kernel_code;
    gdt_entry_t     kernel_data;
    gdt_entry_t     user_code;
    gdt_entry_t     user_data;
    gdt_tss_entry_t tss;
} __attribute__((packed)) gdt_table_t;

static gdt_table_t gdt;
static gdt_ptr_t   gdt_ptr;
static tss_t       tss;

static gdt_entry_t make_entry(uint32_t base, uint32_t limit,
                               uint8_t access, uint8_t gran)
{
    gdt_entry_t e;
    e.limit_low   = limit & 0xFFFF;
    e.base_low    = base  & 0xFFFF;
    e.base_mid    = (base >> 16) & 0xFF;
    e.access      = access;
    e.granularity = (gran & 0xF0) | ((limit >> 16) & 0x0F);
    e.base_high   = (base >> 24) & 0xFF;
    return e;
}

static gdt_tss_entry_t make_tss_entry(uint64_t base, uint32_t limit)
{
    gdt_tss_entry_t e;
    e.low.limit_low   = limit & 0xFFFF;
    e.low.base_low    = base  & 0xFFFF;
    e.low.base_mid    = (base >> 16) & 0xFF;
    e.low.access      = GDT_PRESENT | GDT_TSS_TYPE;
    e.low.granularity = (limit >> 16) & 0x0F;
    e.low.base_high   = (base >> 24) & 0xFF;
    e.base_upper      = (base >> 32) & 0xFFFFFFFF;
    e.reserved        = 0;
    return e;
}

extern void gdt_flush(uint64_t ptr);
extern void tss_flush(void);

void gdt_init(void)
{
    /* Zero TSS */
    uint8_t *p = (uint8_t*)&tss;
    for (uint32_t i = 0; i < sizeof(tss_t); i++) p[i] = 0;
    tss.iomap_base = sizeof(tss_t);

    /* Fill descriptors */
    gdt.null        = make_entry(0, 0, 0, 0);
    gdt.kernel_code = make_entry(0, 0xFFFFF,
        GDT_PRESENT | GDT_RING0 | GDT_CODE_DATA | GDT_EXECUTABLE | GDT_READABLE,
        GDT_GRAN_4K | GDT_LONG_MODE);
    gdt.kernel_data = make_entry(0, 0xFFFFF,
        GDT_PRESENT | GDT_RING0 | GDT_CODE_DATA | GDT_WRITABLE,
        GDT_GRAN_4K | GDT_32BIT);
    gdt.user_code   = make_entry(0, 0xFFFFF,
        GDT_PRESENT | GDT_RING3 | GDT_CODE_DATA | GDT_EXECUTABLE | GDT_READABLE,
        GDT_GRAN_4K | GDT_LONG_MODE);
    gdt.user_data   = make_entry(0, 0xFFFFF,
        GDT_PRESENT | GDT_RING3 | GDT_CODE_DATA | GDT_WRITABLE,
        GDT_GRAN_4K | GDT_32BIT);
    gdt.tss         = make_tss_entry((uint64_t)&tss, sizeof(tss_t) - 1);

    /* Load GDT pointer */
    gdt_ptr.limit = sizeof(gdt_table_t) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;

    gdt_flush((uint64_t)&gdt_ptr);
    tss_flush();
}

void tss_set_kernel_stack(uint64_t stack)
{
    tss.rsp0 = stack;
}
