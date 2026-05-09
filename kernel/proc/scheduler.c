#include <kernel/process.h>
#include <kernel/heap.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>

/* Defined in switch.asm */
extern void switch_context(uint64_t* old_rsp_ptr, uint64_t new_rsp);
extern void process_trampoline(void);

static process_t* current_process = 0;
static process_t* process_list    = 0;
static uint32_t   next_pid        = 1;
static uint64_t   total_ticks     = 0;

/* -----------------------------------------------------------------------
 * setup_stack — build a fake switch_context frame on a brand-new stack
 *
 * switch_context pops in this order (low addr → high addr):
 *   [rsp+0]  r15
 *   [rsp+8]  r14
 *   [rsp+16] r13
 *   [rsp+24] r12
 *   [rsp+32] rbx  ← we put entry here so trampoline can call rbx
 *   [rsp+40] rbp
 *   [rsp+48] ret addr → process_trampoline
 *
 * stack_top must be BASE + PROCESS_STACK_SIZE (one past end of allocation).
 * ----------------------------------------------------------------------- */
static void setup_stack(process_t* p, void (*entry)(void)) {
    /* Start at the top of the stack, align down to 16 bytes */
    uint64_t* stk = (uint64_t*)p->stack_top;
    stk = (uint64_t*)((uint64_t)stk & ~(uint64_t)0xF);

    /* Push in reverse order (pre-decrement = high addr first) */
    *--stk = (uint64_t)process_trampoline; /* ret addr  [+48] */
    *--stk = 0;                             /* rbp       [+40] */
    *--stk = (uint64_t)entry;              /* rbx       [+32] */
    *--stk = 0;                             /* r12       [+24] */
    *--stk = 0;                             /* r13       [+16] */
    *--stk = 0;                             /* r14       [+8]  */
    *--stk = 0;                             /* r15       [+0]  */

    p->context.kernel_rsp = (uint64_t)stk;
}

/* ----------------------------------------------------------------------- */

void scheduler_init(void) {
    process_t* kp = (process_t*)kzalloc(sizeof(process_t));
    kp->pid   = 1;
    kp->state = PROCESS_RUNNING;
    kp->name[0]='k'; kp->name[1]='e'; kp->name[2]='r';
    kp->name[3]='n'; kp->name[4]='e'; kp->name[5]='l';
    /* Kernel process uses the current stack — no setup_stack needed */
    kp->stack_base = 0;
    kp->stack_top  = 0;
    process_list    = kp;
    kp->next        = kp;
    current_process = kp;
    next_pid        = 2;
}

process_t* process_create(const char* name, void (*entry)(void)) {
    process_t* p = (process_t*)kzalloc(sizeof(process_t));
    if (!p) return 0;

    /* Allocate stack: PMM gives one page (4096 bytes).
     * stack_base = page base address (e.g. 0xA0000)
     * stack_top  = base + size        (e.g. 0xA1000)  ← CRITICAL */
    void* stack_page = pmm_alloc_page();
    if (!stack_page) { kfree(p); return 0; }

    p->pid        = next_pid++;
    p->state      = PROCESS_READY;
    p->stack_base = (uint64_t)stack_page;
    p->stack_top  = (uint64_t)stack_page + PAGE_SIZE;   /* past end of page */

    int i = 0;
    while (name[i] && i < PROCESS_NAME_MAX - 1) { p->name[i] = name[i]; i++; }

    setup_stack(p, entry);

    /* Append to circular list */
    process_t* t = process_list;
    while (t->next != process_list) t = t->next;
    t->next = p;
    p->next = process_list;

    return p;
}

/* -----------------------------------------------------------------------
 * do_switch — pick next READY process and swap stacks
 * ----------------------------------------------------------------------- */
static void do_switch(void) {
    /* Find next READY process after current */
    process_t* next = current_process->next;
    int loops = 0;
    while (next->state != PROCESS_READY && next != current_process) {
        next = next->next;
        if (++loops > MAX_PROCESSES) return;   /* nothing to switch to */
    }
    if (next == current_process) return;       /* only one runnable process */

    process_t* old  = current_process;
    current_process = next;

    if (old->state == PROCESS_RUNNING) old->state = PROCESS_READY;
    next->state = PROCESS_RUNNING;

    /* Hand off — this saves old RSP and loads next RSP */
    switch_context(&old->context.kernel_rsp, next->context.kernel_rsp);
    /* Execution resumes here when old process is switched back in */
}

void process_yield(void) {
    __asm__ volatile ("cli");
    do_switch();
    __asm__ volatile ("sti");
}

void scheduler_tick(void) {
    total_ticks++;
    if (!process_list) return;
    current_process->ticks++;

    /* Wake sleeping processes */
    process_t* p = process_list;
    do {
        if (p->state == PROCESS_SLEEPING && total_ticks >= p->wake_tick)
            p->state = PROCESS_READY;
        p = p->next;
    } while (p != process_list);
}

void process_sleep(uint64_t ticks) {
    current_process->wake_tick = total_ticks + ticks;
    current_process->state     = PROCESS_SLEEPING;
    process_yield();
}

void process_exit(void) {
    __asm__ volatile ("cli");
    current_process->state = PROCESS_DEAD;
    do_switch();           /* switch away — never returns here */
    while (1) __asm__ volatile ("hlt");
}

process_t* process_current(void)          { return current_process; }
uint64_t   scheduler_get_ticks(void)      { return total_ticks; }

process_t* process_get(uint32_t pid) {
    process_t* p = process_list;
    if (!p) return 0;
    do { if (p->pid == pid) return p; p = p->next; } while (p != process_list);
    return 0;
}
