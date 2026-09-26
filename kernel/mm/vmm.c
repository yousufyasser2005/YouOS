/*
 * YouOS - Virtual Memory Manager
 * vmm.c - Fixed version
 *
 * Key insight: our boot page tables use 2MB HUGE pages for the identity map.
 * When vmm_map tries to walk through them to create a PT level,
 * it must replace the huge page with a proper PT table first.
 */

#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>

address_space_t kernel_as;

/* Allocate and zero a page table */
static pte_t* alloc_table(void)
{
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    pte_t* t = (pte_t*)phys;
    for (int i = 0; i < 512; i++) t[i] = 0;
    return t;
}

/*
 * Get or create next level table.
 * If entry is a huge page, replace it with a proper PT.
 */
static pte_t* get_or_create(pte_t* table, uint64_t idx, uint64_t flags)
{
    pte_t entry = table[idx];

    /* Entry present and NOT a huge page — just return it */
    if ((entry & PTE_PRESENT) && !(entry & PTE_HUGE)) {
        return (pte_t*)(entry & PTE_ADDR_MASK);
    }

    /* Entry is a huge page — we need to split it into 4KB pages */
    if ((entry & PTE_PRESENT) && (entry & PTE_HUGE)) {
        pte_t* new_pt = alloc_table();
        if (!new_pt) return 0;

        /* Get the 2MB base address this huge page covers */
        uint64_t huge_base = entry & 0x000FFFFFFFE00000ULL;
        uint64_t pt_flags  = (entry & 0xFFF) & ~PTE_HUGE;

        /* Fill new PT with 4KB pages covering same physical range,
         * preserving the ORIGINAL huge page's own permission bits
         * (pt_flags) -- NOT the generic 'flags' parameter this
         * function was called with for an unrelated lookup/create at
         * this level. Using 'flags' here was a real bug: it silently
         * widened every split page to whatever the triggering caller
         * happened to request (in practice always PTE_WRITABLE|
         * PTE_USER, per vmm_map()'s call sites), discarding the
         * original page's real protection (e.g. kernel-only or
         * read-only memory could become user-writable the moment it
         * got split). */
        for (int i = 0; i < 512; i++) {
            new_pt[i] = (huge_base + (uint64_t)i * PAGE_SIZE)
                       | pt_flags | PTE_PRESENT;
        }

        /* Replace huge page entry with new PT */
        table[idx] = (uint64_t)new_pt | flags | PTE_PRESENT;
        return new_pt;
    }

    /* Entry not present — allocate new table */
    pte_t* new_table = alloc_table();
    if (!new_table) return 0;
    table[idx] = (uint64_t)new_table | flags | PTE_PRESENT;
    return new_table;
}

int vmm_map(address_space_t* as, uint64_t virt, uint64_t phys, uint64_t flags)
{
    pte_t* pml4 = as->pml4;

    pte_t* pdp = get_or_create(pml4, PML4_INDEX(virt),
                                PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    if (!pdp) return -1;

    pte_t* pd  = get_or_create(pdp,  PDP_INDEX(virt),
                                PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    if (!pd) return -1;

    pte_t* pt  = get_or_create(pd,   PD_INDEX(virt),
                                PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    if (!pt) return -1;

    pt[PT_INDEX(virt)] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;

    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

void vmm_unmap(address_space_t* as, uint64_t virt)
{
    pte_t* pml4 = as->pml4;
    if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) return;

    pte_t* pdp = (pte_t*)(pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);
    if (!(pdp[PDP_INDEX(virt)] & PTE_PRESENT)) return;

    pte_t* pd  = (pte_t*)(pdp[PDP_INDEX(virt)] & PTE_ADDR_MASK);
    if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) return;
    if (pd[PD_INDEX(virt)] & PTE_HUGE) return;  /* don't unmap huge pages */

    pte_t* pt  = (pte_t*)(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);
    pt[PT_INDEX(virt)] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t vmm_get_phys(address_space_t* as, uint64_t virt)
{
    pte_t* pml4 = as->pml4;
    if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) return 0;

    pte_t* pdp = (pte_t*)(pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);
    if (!(pdp[PDP_INDEX(virt)] & PTE_PRESENT)) return 0;

    pte_t* pd  = (pte_t*)(pdp[PDP_INDEX(virt)] & PTE_ADDR_MASK);
    if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) return 0;
    if (pd[PD_INDEX(virt)] & PTE_HUGE)
        return (pd[PD_INDEX(virt)] & 0x000FFFFFFFE00000ULL) + (virt & 0x1FFFFF);

    pte_t* pt  = (pte_t*)(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);
    if (!(pt[PT_INDEX(virt)] & PTE_PRESENT)) return 0;
    return (pt[PT_INDEX(virt)] & PTE_ADDR_MASK) + (virt & 0xFFF);
}

void vmm_switch(address_space_t* as)
{
    __asm__ volatile (
        "mov %0, %%cr3"
        : : "r"((uint64_t)as->pml4) : "memory"
    );
}

/* Linker-provided boundaries of the kernel's own executable code -- see
 * kernel/linker.ld. Everything else in the identity map (the multiboot2
 * header, .rodata, .data, .bss, the embedded initrd, the kernel heap,
 * the PMM's own bitmap, and any unused physical RAM up to the 1GB
 * identity-map limit) is data, never code. */
extern uint8_t _text_start[];
extern uint8_t _text_end[];

/* Builds (or, for a fresh user address space, rebuilds an identical
 * copy of) the identity map's PD entry at granule index `i` -- either a
 * plain 2MB huge page (executable only if the WHOLE granule is inside
 * the kernel's own .text, NX otherwise), or, for the one or two
 * granules that straddle the .text/data boundary (a 2MB granule can't
 * represent mixed exec/NX permissions on its own), a split into 4KB
 * pages with NX decided individually per page.
 *
 * Shared between vmm_init() (building kernel_as's own PD once, at
 * boot, before any process exists) and vmm_create_user_as() (rebuilding
 * the SAME deterministic result fresh for every new process). It's
 * deliberately recomputed rather than deep-copied from kernel_as's live
 * PD for this one specific, boot-time split -- see
 * vmm_create_user_as()'s own comment for why simply copying would leave
 * a hole in every process's identity map exactly where kernel code/data
 * lives, which would page-fault the very first syscall trap taken under
 * that process's own CR3 (SYSCALL doesn't reload CR3). */
static void build_identity_granule(pte_t* pd, int i) {
    uint64_t text_start = (uint64_t)_text_start;
    uint64_t text_end   = (uint64_t)_text_end;

    uint64_t granule_start = (uint64_t)i << 21;
    uint64_t granule_end   = granule_start + (1ULL << 21);
    uint64_t nx = nx_supported ? PTE_NO_EXEC : 0;

    int overlaps_text = (granule_start < text_end) && (granule_end > text_start);
    if (!overlaps_text) {
        /* Entirely outside .text -- data, NX. */
        pd[i] = granule_start | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE | nx;
        return;
    }
    if (granule_start >= text_start && granule_end <= text_end) {
        /* Entirely inside .text -- executable, exactly as before this
         * fix (this codebase doesn't currently distinguish "writable"
         * from "executable" for kernel code beyond NX, matching the
         * same scope the original ELF-segment NX hardening used --
         * PF_X vs not -- rather than introducing a new, unprecedented
         * read-only-code scheme here). */
        pd[i] = granule_start | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE;
        return;
    }

    /* Straddles the boundary -- split into 4KB pages, NX decided
     * per-page. Reuses the same "allocate a PT, fill it with 4KB
     * entries covering the same physical range" shape get_or_create()
     * already uses elsewhere in this file for splitting a huge page
     * on demand -- this is just done proactively, once, at the exact
     * one or two indices that need it, instead of reactively. */
    pte_t* pt = alloc_table();
    if (!pt) {
        /* OOM this early in boot is effectively unrecoverable anyway;
         * fall back to the pre-fix behavior (huge, executable, no NX)
         * for this one granule rather than leaving it unmapped. */
        pd[i] = granule_start | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE;
        return;
    }
    for (int j = 0; j < 512; j++) {
        uint64_t page_addr = granule_start + (uint64_t)j * PAGE_SIZE;
        int in_text = (page_addr >= text_start) && (page_addr < text_end);
        pt[j] = page_addr | PTE_PRESENT | PTE_WRITABLE | (in_text ? 0 : nx);
    }
    pd[i] = (uint64_t)pt | PTE_PRESENT | PTE_WRITABLE;
}

void vmm_init(void)
{
    /* Allocate PML4 */
    kernel_as.pml4      = alloc_table();
    kernel_as.pml4_phys = (uint64_t)kernel_as.pml4;

    if (!kernel_as.pml4) {
        vga_puts_color("  [!!] VMM: PML4 alloc failed!\n",
                       VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    /*
     * Identity map first 1GB using 2MB huge pages.
     * This keeps the kernel running after CR3 switch.
     * We map it as a PD directly under PDP[0] → PML4[0].
     */
    pte_t* pdp = alloc_table();
    if (!pdp) return;
    kernel_as.pml4[0] = (uint64_t)pdp | PTE_PRESENT | PTE_WRITABLE | PTE_USER;

    pte_t* pd = alloc_table();
    if (!pd) return;
    pdp[0] = (uint64_t)pd | PTE_PRESENT | PTE_WRITABLE | PTE_USER;

    /* 512 × 2MB huge pages = 1GB identity map, now NX everywhere except
     * the kernel's own .text -- see build_identity_granule()'s comment
     * for the full mechanism. This was the one concrete data surface
     * the original NX-hardening pass explicitly left uncovered
     * ("distinguishing kernel code from kernel data within that shared
     * identity map would need more substantial restructuring than this
     * change") -- this is that restructuring. */
    for (int i = 0; i < 512; i++) {
        build_identity_granule(pd, i);
    }

    /* Switch to new page tables */
    vmm_switch(&kernel_as);
}

/*
 * Create a new user address space.
 * Copies all kernel PML4 entries (indices 1-511) from kernel_as.
 * Index 0 is left empty — user mappings go here per-process.
 */
address_space_t vmm_create_user_as(void)
{
    address_space_t as;
    as.pml4 = alloc_table();
    as.pml4_phys = (uint64_t)as.pml4;

    if (!as.pml4) return as;

    /* Copy kernel PML4 entries (skip index 0 which is user space) */
    for (int i = 1; i < 512; i++)
        as.pml4[i] = kernel_as.pml4[i];

    /* Index 0: deep-copy PDP+PD so each process has its own private PD.
     * Copy ALL huge-page entries (full identity map stays intact so kernel
     * can access all physical memory after vmm_switch).
     * Because each process has its own PD, vmm_map splitting pd[2] for the
     * ELF at 0x400000 only affects that process — not the kernel or shell. */
    pte_t* kpdp = (pte_t*)(kernel_as.pml4[0] & PTE_ADDR_MASK);
    if (kpdp) {
        pte_t* new_pdp = alloc_table();
        if (new_pdp) {
            for (int i = 0; i < 512; i++) {
                if (!(kpdp[i] & PTE_PRESENT)) continue;
                pte_t* kpd = (pte_t*)(kpdp[i] & PTE_ADDR_MASK);
                pte_t* new_pd = alloc_table();
                if (!new_pd) continue;
                /* Copy all entries — huge pages only (not split PTs).
                   If kpd[j] is already a PT (was split for a previous process),
                   skip it — this process gets a fresh huge page instead.
                 *
                 * EXCEPTION, added for NX coverage: a present-but-not-huge
                 * kpd[j] is either (a) the kernel's own .text/NX boundary
                 * split, set up once by vmm_init() before ANY process
                 * exists (see build_identity_granule()'s comment), or (b)
                 * some other, unrelated runtime split of kernel_as's PD
                 * from elsewhere (e.g. heap_init()'s later growth). Case
                 * (b) keeps the pre-existing behavior above unchanged
                 * (left at 0/not present -- a pre-existing limitation this
                 * fix doesn't touch). Case (a) CANNOT be left as a hole:
                 * unlike (b), it's not something "this process gets a
                 * fresh huge page instead" for -- it's kernel code/data
                 * that must be identically present in every address
                 * space, or the very first syscall trap taken under this
                 * new process's own CR3 (SYSCALL doesn't reload CR3)
                 * would immediately page-fault. Recomputed fresh via the
                 * same deterministic function, rather than trying to
                 * literally copy kpd[j]'s own PT (whose entries could, in
                 * principle, be case (b)'s too, on a different index) --
                 * distinguishing the two by re-deriving the boundary from
                 * the linker symbols directly is simpler and cannot drift
                 * out of sync with vmm_init()'s own logic. */
                for (int j = 0; j < 512; j++) {
                    if ((kpd[j] & PTE_PRESENT) && (kpd[j] & PTE_HUGE)) {
                        new_pd[j] = kpd[j];   /* copy huge page as-is */
                    } else if (kpd[j] & PTE_PRESENT) {
                        uint64_t gs = (uint64_t)j << 21;
                        uint64_t ge = gs + (1ULL << 21);
                        uint64_t ts = (uint64_t)_text_start, te = (uint64_t)_text_end;
                        int straddles_text = (gs < te) && (ge > ts) &&
                                              !(gs >= ts && ge <= te);
                        if (straddles_text) build_identity_granule(new_pd, j);
                        /* else leave 0 — vmm_map creates a fresh PT */
                    }
                    /* else leave 0 — vmm_map creates a fresh PT */
                }
                new_pdp[i] = ((uint64_t)new_pd) | (kpdp[i] & 0xFFF);
            }
            as.pml4[0] = ((uint64_t)new_pdp) | (kernel_as.pml4[0] & 0xFFF);
        }
    }

    /* Map framebuffer into every user address space */
    extern void fb_map_into_as(address_space_t*);
    fb_map_into_as(&as);
    return as;
}

/*
 * Destroy a user address space — frees every PDP/PD/PT page-table
 * STRUCTURE under PML4[0] (the user region), plus the PML4 itself.
 * Does NOT free the physical pages backing actual user DATA (ELF
 * segments, the ring-3 stack) -- those are owned by the caller
 * (process_reap(), which frees them separately via vmm_get_phys()
 * against the still-intact page tables, BEFORE calling this) since
 * this function only has an address_space_t to work with, not the
 * process_t that knows which virtual ranges are this process's own
 * private data versus shared (e.g. the framebuffer, mapped into every
 * address space -- its physical pages must never be freed here, only
 * the page-table entries that reference them).
 *
 * Previously this only zeroed PML4[0]'s contents without freeing
 * anything at all -- a real, unbounded leak: every process that ever
 * ran and got reaped leaked its entire page-table tree (PDP + every
 * PD + every split PT + the PML4 page itself) permanently. On a
 * system with ~256MB total and a hard PMM cap to match, repeated
 * process creation (exactly what real use of this session's
 * concurrency work encourages -- opening/closing terminal windows,
 * spawntest/killtest/iotest, etc.) would eventually exhaust physical
 * memory entirely.
 */
void vmm_destroy_user_as(address_space_t* as)
{
    if (!as->pml4) return;

    pte_t pml4e = as->pml4[0];
    if (!(pml4e & PTE_PRESENT)) goto free_pml4;

    /* Defensive: PML4[0] should never actually equal kernel_as's own
     * shared PDP pointer -- vmm_create_user_as() always deep-copies
     * into a fresh PDP -- but if some future change ever left it
     * aliased, freeing it here would take down every other address
     * space in the system sharing it. Structurally shouldn't happen;
     * kept as a belt-and-suspenders guard rather than assumed away. */
    if ((pml4e & PTE_ADDR_MASK) == (kernel_as.pml4[0] & PTE_ADDR_MASK))
        goto free_pml4;

    {
        pte_t* pdp = (pte_t*)(pml4e & PTE_ADDR_MASK);
        for (int i = 0; i < 512; i++) {
            pte_t pdpe = pdp[i];
            if (!(pdpe & PTE_PRESENT) || (pdpe & PTE_HUGE)) continue;
            pte_t* pd = (pte_t*)(pdpe & PTE_ADDR_MASK);
            for (int j = 0; j < 512; j++) {
                pte_t pde = pd[j];
                /* A present, non-huge PD entry is a split PT --
                 * always a fresh, process-private allocation from
                 * get_or_create() (e.g. splitting a huge identity-map
                 * page to map an ELF segment, the ring-3 stack, or
                 * the framebuffer at finer granularity). Free the
                 * TABLE itself here; the DATA pages its entries point
                 * to are handled by the caller as described above. */
                if (!(pde & PTE_PRESENT) || (pde & PTE_HUGE)) continue;
                pte_t* pt = (pte_t*)(pde & PTE_ADDR_MASK);
                pmm_free_page((uint64_t)pt);
            }
            pmm_free_page((uint64_t)pd);
        }
        pmm_free_page((uint64_t)pdp);
    }

free_pml4:
    pmm_free_page((uint64_t)as->pml4);
    as->pml4 = 0;
}

int vmm_map_in(address_space_t* as, uint64_t virt, uint64_t phys, uint64_t flags)
{
    return vmm_map(as, virt, phys, flags);
}
