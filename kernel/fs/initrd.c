#include <kernel/initrd.h>
#include <kernel/vga.h>

/* Initrd is embedded in the kernel via linker symbol */
extern uint8_t _initrd_start[];
extern uint8_t _initrd_end[];

static const uint8_t* initrd_data = 0;
static uint64_t       initrd_size = 0;

void initrd_init(void) {
    initrd_data = _initrd_start;
    initrd_size = (uint64_t)(_initrd_end - _initrd_start);

    const initrd_header_t* hdr = (const initrd_header_t*)initrd_data;
    if (hdr->magic != INITRD_MAGIC) {
        vga_puts_color("  [!!] initrd: bad magic\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    vga_puts_color("  [OK] initrd initialized (", VGA_LIGHT_GREEN, VGA_BLACK);
    /* print file count — simple decimal */
    char buf[8]; int i = 7; buf[i] = 0;
    uint32_t n = hdr->count;
    if (n == 0) { buf[--i] = '0'; }
    while (n > 0) { buf[--i] = '0' + (n % 10); n /= 10; }
    vga_puts_color(&buf[i], VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts_color(" files)\n", VGA_LIGHT_GREEN, VGA_BLACK);
}

const void* initrd_find(const char* name, uint64_t* size) {
    if (!initrd_data) return 0;
    const initrd_header_t* hdr = (const initrd_header_t*)initrd_data;
    if (hdr->magic != INITRD_MAGIC) return 0;

    const initrd_entry_t* entries = (const initrd_entry_t*)(initrd_data + sizeof(initrd_header_t));
    for (uint32_t i = 0; i < hdr->count; i++) {
        /* Compare names */
        const char* a = name;
        const char* b = entries[i].name;
        int match = 1;
        while (*a && *b) { if (*a++ != *b++) { match = 0; break; } }
        if (match && *a == 0 && *b == 0) {
            *size = entries[i].size;
            return initrd_data + entries[i].offset;
        }
    }
    return 0;
}
