#include <kernel/pmm.h>
#include <kernel/vga.h>

#define BITMAP_ADDR  0x500000
#define MAX_PAGES    65536      /* 256MB / 4KB = 65536 pages max */

static uint8_t*  bitmap      = (uint8_t*)BITMAP_ADDR;
static uint64_t  total_pages = 0;
static uint64_t  free_pages  = 0;

static inline void bitmap_set(uint64_t pfn)   { if(pfn<MAX_PAGES) bitmap[pfn/8] |=  (1<<(pfn%8)); }
static inline void bitmap_clear(uint64_t pfn) { if(pfn<MAX_PAGES) bitmap[pfn/8] &= ~(1<<(pfn%8)); }
static inline int  bitmap_test(uint64_t pfn)  { if(pfn>=MAX_PAGES) return 1; return (bitmap[pfn/8]>>(pfn%8))&1; }

void pmm_init(uint64_t mb2_info_addr)
{
    /* Mark everything used */
    for (uint64_t i = 0; i < MAX_PAGES/8; i++) bitmap[i] = 0xFF;

    uint8_t*   mb2        = (uint8_t*)mb2_info_addr;
    uint32_t   total_size = *(uint32_t*)mb2;
    mb2_tag_t* tag        = (mb2_tag_t*)(mb2 + 8);

    while ((uint8_t*)tag < mb2 + total_size) {
        if (tag->type == MB2_TAG_END) break;

        if (tag->type == MB2_TAG_MEMMAP) {
            mb2_tag_memmap_t* mt  = (mb2_tag_memmap_t*)tag;
            uint8_t* ep  = (uint8_t*)mt + sizeof(mb2_tag_memmap_t);
            uint8_t* end = (uint8_t*)tag + tag->size;

            while (ep < end) {
                mb2_memmap_entry_t* e = (mb2_memmap_entry_t*)ep;

                /* Only count memory below 1GB */
                if (e->base_addr < 0x40000000ULL) {
                    uint64_t region_end = e->base_addr + e->length;
                    if (region_end > 0x40000000ULL)
                        region_end = 0x40000000ULL;

                    uint64_t pfn_end = ADDR_TO_PFN(region_end);
                    if (pfn_end > MAX_PAGES) pfn_end = MAX_PAGES;
                    if (pfn_end > total_pages) total_pages = pfn_end;

                    if (e->type == MB2_MEM_AVAILABLE) {
                        uint64_t pfn_start = ADDR_TO_PFN(e->base_addr);
                        for (uint64_t pfn = pfn_start; pfn < pfn_end; pfn++) {
                            bitmap_clear(pfn);
                            free_pages++;
                        }
                    }
                }
                ep += mt->entry_size;
            }
        }

        uint32_t next = tag->size;
        if (next % 8) next += 8 - (next % 8);
        tag = (mb2_tag_t*)((uint8_t*)tag + next);
    }

    /* Reserve first 6MB (kernel + bitmap + mb2 info) */
    for (uint64_t pfn = 0; pfn < ADDR_TO_PFN(0x600000); pfn++) {
        if (!bitmap_test(pfn)) { bitmap_set(pfn); free_pages--; }
    }

    /* Reserve VGA buffer */
    if (!bitmap_test(ADDR_TO_PFN(0xB8000))) {
        bitmap_set(ADDR_TO_PFN(0xB8000));
        free_pages--;
    }
}

uint64_t pmm_alloc_page(void)
{
    for (uint64_t pfn = ADDR_TO_PFN(0x600000); pfn < total_pages; pfn++) {
        if (!bitmap_test(pfn)) {
            bitmap_set(pfn);
            free_pages--;
            return PFN_TO_ADDR(pfn);
        }
    }
    return 0;
}

uint64_t pmm_alloc_pages(size_t n)
{
    uint64_t start = 0; size_t count = 0;
    for (uint64_t pfn = ADDR_TO_PFN(0x600000); pfn < total_pages; pfn++) {
        if (!bitmap_test(pfn)) {
            if (count == 0) start = pfn;
            if (++count == n) {
                for (uint64_t i = start; i < start+n; i++) { bitmap_set(i); free_pages--; }
                return PFN_TO_ADDR(start);
            }
        } else { count = 0; }
    }
    return 0;
}

void pmm_free_page(uint64_t addr)
{
    uint64_t pfn = ADDR_TO_PFN(addr);
    if (pfn < ADDR_TO_PFN(0x600000) || pfn >= total_pages) return;
    if (bitmap_test(pfn)) { bitmap_clear(pfn); free_pages++; }
}

pmm_stats_t pmm_get_stats(void)
{
    pmm_stats_t s;
    s.total_pages = total_pages;
    s.free_pages  = free_pages;
    s.used_pages  = total_pages - free_pages;
    s.total_bytes = total_pages * PAGE_SIZE;
    return s;
}
