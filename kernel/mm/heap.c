/*
 * YouOS - Kernel Heap Allocator
 * heap.c
 *
 * Free-list allocator with block coalescing.
 *
 * Layout in memory:
 *   HEAP_START
 *   [ header | data ] [ header | data ] [ header | data (free) ] ...
 *   HEAP_START + HEAP_INIT_SIZE
 *
 * Strategy:
 *   - First-fit: scan from start, return first free block that fits
 *   - Split: if free block is much larger than needed, split it
 *   - Coalesce: on free, merge adjacent free blocks to prevent fragmentation
 */

#include <kernel/heap.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>

/* Heap state */
static block_header_t* heap_start_block = 0;
static uint64_t        heap_brk         = HEAP_START;
static uint64_t        heap_mapped_end  = HEAP_START;

/* Minimum split size — don't split if remainder < this */
#define MIN_SPLIT_SIZE  (HEADER_SIZE + 16)

/* =========================================================================
 * Internal: map more physical pages into the heap virtual region
 * ========================================================================= */
static int heap_expand(uint64_t bytes)
{
    /* Round up to page boundary */
    bytes = (bytes + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);

    uint64_t new_end = heap_mapped_end + bytes;
    if (new_end > HEAP_END) return -1;  /* Out of heap space */

    for (uint64_t addr = heap_mapped_end; addr < new_end; addr += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return -1;
        vmm_map(&kernel_as, addr, phys, PTE_PRESENT | PTE_WRITABLE);
    }

    heap_mapped_end = new_end;
    return 0;
}

/* =========================================================================
 * Internal: zero memory
 * ========================================================================= */
static void mem_zero(void* ptr, size_t n) {
    uint8_t* p = (uint8_t*)ptr;
    while (n--) *p++ = 0;
}

/* =========================================================================
 * heap_init — map initial heap pages and create first free block
 * ========================================================================= */
void heap_init(void)
{
    /* Map initial heap pages */
    if (heap_expand(HEAP_INIT_SIZE) != 0) {
        vga_puts_color("  [!!] heap_init: failed to map pages!\n",
                       VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    /* Create one giant free block covering entire initial heap */
    heap_start_block        = (block_header_t*)HEAP_START;
    heap_start_block->magic = HEAP_MAGIC;
    heap_start_block->size  = HEAP_INIT_SIZE - HEADER_SIZE;
    heap_start_block->free  = 1;
    heap_start_block->next  = 0;
    heap_start_block->prev  = 0;

    heap_brk = HEAP_START + HEAP_INIT_SIZE;
}

/* =========================================================================
 * kmalloc — first-fit allocation with splitting
 * ========================================================================= */
void* kmalloc(size_t size)
{
    if (size == 0) return 0;

    /* Align size to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    /* Walk free list */
    block_header_t* block = heap_start_block;
    while (block) {
        if (block->magic != HEAP_MAGIC) {
            vga_puts_color("  [!!] kmalloc: heap corruption!\n",
                           VGA_LIGHT_RED, VGA_BLACK);
            return 0;
        }

        if (block->free && block->size >= size) {
            /* Found a fit — split if block is much larger */
            if (block->size >= size + MIN_SPLIT_SIZE) {
                block_header_t* new_block =
                    (block_header_t*)((uint8_t*)block + HEADER_SIZE + size);
                new_block->magic = HEAP_MAGIC;
                new_block->size  = block->size - size - HEADER_SIZE;
                new_block->free  = 1;
                new_block->next  = block->next;
                new_block->prev  = block;

                if (block->next) block->next->prev = new_block;
                block->next = new_block;
                block->size = size;
            }

            block->free = 0;
            return (void*)((uint8_t*)block + HEADER_SIZE);
        }

        block = block->next;
    }

    /* No fit found — expand heap */
    size_t expand = size + HEADER_SIZE;
    if (expand < PAGE_SIZE) expand = PAGE_SIZE;

    if (heap_expand(expand) != 0) return 0;

    /* Create new block at current brk */
    block_header_t* new_block = (block_header_t*)heap_brk;
    new_block->magic = HEAP_MAGIC;
    new_block->size  = expand - HEADER_SIZE;
    new_block->free  = 1;
    new_block->next  = 0;

    /* Link to last block */
    block_header_t* last = heap_start_block;
    while (last->next) last = last->next;
    last->next      = new_block;
    new_block->prev = last;

    heap_brk += expand;

    /* Retry allocation */
    return kmalloc(size);
}

/* =========================================================================
 * kzalloc — allocate and zero
 * ========================================================================= */
void* kzalloc(size_t size)
{
    void* ptr = kmalloc(size);
    if (ptr) mem_zero(ptr, size);
    return ptr;
}

/* =========================================================================
 * kmalloc_aligned — allocate with alignment
 * ========================================================================= */
void* kmalloc_aligned(size_t size, size_t align)
{
    if (align <= 8) return kmalloc(size);

    /* Allocate extra space to find an aligned address within */
    void* raw = kmalloc(size + align + HEADER_SIZE);
    if (!raw) return 0;

    uint64_t addr    = (uint64_t)raw;
    uint64_t aligned = (addr + align - 1) & ~(uint64_t)(align - 1);
    return (void*)aligned;
}

/* =========================================================================
 * kfree — free and coalesce adjacent free blocks
 * ========================================================================= */
void kfree(void* ptr)
{
    if (!ptr) return;

    block_header_t* block =
        (block_header_t*)((uint8_t*)ptr - HEADER_SIZE);

    if (block->magic != HEAP_MAGIC) {
        vga_puts_color("  [!!] kfree: invalid pointer or corruption!\n",
                       VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    if (block->free) {
        vga_puts_color("  [!!] kfree: double free detected!\n",
                       VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    block->free = 1;

    /* Coalesce with next block if it's free */
    if (block->next && block->next->free) {
        block->size += HEADER_SIZE + block->next->size;
        block->next  = block->next->next;
        if (block->next) block->next->prev = block;
    }

    /* Coalesce with previous block if it's free */
    if (block->prev && block->prev->free) {
        block->prev->size += HEADER_SIZE + block->size;
        block->prev->next  = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

/* =========================================================================
 * heap_dump_stats
 * ========================================================================= */
static void print_uint(uint64_t val) {
    char buf[21]; int i = 20; buf[i] = '\0';
    if (val == 0) { vga_puts("0"); return; }
    while (val > 0) { buf[--i] = '0' + (val % 10); val /= 10; }
    vga_puts(&buf[i]);
}

void heap_dump_stats(void)
{
    uint64_t total = 0, used = 0, free_bytes = 0, blocks = 0, free_blocks = 0;

    block_header_t* b = heap_start_block;
    while (b) {
        if (b->magic != HEAP_MAGIC) break;
        blocks++;
        total += b->size;
        if (b->free) { free_bytes += b->size; free_blocks++; }
        else           used += b->size;
        b = b->next;
    }

    vga_puts_color("       Heap: ", VGA_DARK_GREY, VGA_BLACK);
    print_uint(total / 1024);
    vga_puts("KB total, ");
    print_uint(used / 1024);
    vga_puts("KB used, ");
    print_uint(free_bytes / 1024);
    vga_puts("KB free, ");
    print_uint(blocks);
    vga_puts(" blocks (");
    print_uint(free_blocks);
    vga_puts(" free)\n");
}
