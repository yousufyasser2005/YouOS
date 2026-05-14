#include <kernel/userspace.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>
#include <kernel/vga.h>

extern void jump_to_userspace(uint64_t entry, uint64_t stack);

static void map_range_user(uint64_t start, uint64_t end) {
    start &= ~(uint64_t)0xFFF;
    end = (end + 0xFFF) & ~(uint64_t)0xFFF;
    for (uint64_t a = start; a < end; a += PAGE_SIZE)
        vmm_map(&kernel_as, a, a, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
}

user_process_t* user_process_create(const char* name, void (*entry)(void))
{
    (void)name;
    user_process_t* proc = (user_process_t*)kzalloc(sizeof(user_process_t));
    if (!proc) return 0;

    /* Allocate user stack from PMM */
    uint64_t stack_base = pmm_alloc_pages(4);
    if (!stack_base) { kfree(proc); return 0; }

    /* Map stack as user-accessible */
    map_range_user(stack_base, stack_base + 4 * PAGE_SIZE);

    uint64_t fn = (uint64_t)entry;

    /* Map 64 pages around function */
    /* Map only the pages actually containing hello_main — not 64 pages
       which would overlap the kernel stack at ~0x10E000 */
    map_range_user(fn, fn + 2 * PAGE_SIZE);

    /* Map VGA buffer */
    map_range_user(0xB8000, 0xB9000);

    /* kernel stack intentionally not mapped to Ring 3 */

    /* Flush TLB */
    __asm__ volatile (
        "mov %%cr3, %%rax\n"
        "mov %%rax, %%cr3\n"
        : : : "rax", "memory"
    );

    /* Verify mapping */
    uint64_t phys = vmm_get_phys(&kernel_as, fn);
    if (phys == 0) {
        vga_puts_color("  [!!] Mapping FAILED\n", VGA_LIGHT_RED, VGA_BLACK);
        kfree(proc);
        return 0;
    }
    vga_puts_color("  [OK] Mapping verified!\n", VGA_LIGHT_GREEN, VGA_BLACK);

    proc->entry     = fn;
    proc->stack_top = stack_base + 4 * PAGE_SIZE;

    return proc;
}

void user_process_exec(user_process_t* proc)
{
    char hex[17]; hex[16] = 0;
    vga_puts_color("  [DBG] entry=0x", VGA_YELLOW, VGA_BLACK);
    uint64_t val = proc->entry;
    for (int i = 15; i >= 0; i--) { hex[i] = "0123456789ABCDEF"[val & 0xF]; val >>= 4; }
    vga_puts_color(hex, VGA_YELLOW, VGA_BLACK);
    vga_puts_color("  stack=0x", VGA_YELLOW, VGA_BLACK);
    val = proc->stack_top;
    for (int i = 15; i >= 0; i--) { hex[i] = "0123456789ABCDEF"[val & 0xF]; val >>= 4; }
    vga_puts_color(hex, VGA_YELLOW, VGA_BLACK);
    vga_puts_color("\n", VGA_YELLOW, VGA_BLACK);
    jump_to_userspace(proc->entry, proc->stack_top);
}

void user_process_destroy(user_process_t* proc)
{
    if (!proc) return;
    kfree(proc);
}
