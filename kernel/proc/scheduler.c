#include <kernel/process.h>
#include <kernel/heap.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>

extern void switch_context(uint64_t* old_rsp_ptr, uint64_t new_rsp);
extern void process_trampoline(void);

static process_t* current_process = 0;
static process_t* process_list    = 0;
static uint32_t   next_pid        = 1;
static uint64_t   total_ticks     = 0;

/* Timeslice: how many timer ticks each process runs before preemption.
 * At 100Hz PIT, 4 ticks = ~40ms per slice — responsive but not too chatty. */
#define TIMESLICE 4

static void setup_stack(process_t* p, void (*entry)(void)) {
    uint64_t* stk = (uint64_t*)p->stack_top;
    stk = (uint64_t*)((uint64_t)stk & ~(uint64_t)0xF);
    *--stk = (uint64_t)process_trampoline;
    *--stk = 0;
    *--stk = (uint64_t)entry;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    p->context.kernel_rsp = (uint64_t)stk;
}

void scheduler_init(void) {
    process_t* kp = (process_t*)kzalloc(sizeof(process_t));
    kp->pid   = 1;
    kp->state = PROCESS_RUNNING;
    kp->name[0]='k'; kp->name[1]='e'; kp->name[2]='r';
    kp->name[3]='n'; kp->name[4]='e'; kp->name[5]='l';
    kp->stack_base  = 0;
    kp->stack_top   = 0;
    kp->timeslice   = TIMESLICE;
    process_list    = kp;
    kp->next        = kp;
    current_process = kp;
    next_pid        = 2;
}

process_t* process_create(const char* name, void (*entry)(void)) {
    process_t* p = (process_t*)kzalloc(sizeof(process_t));
    if (!p) return 0;

    void* stack_page = pmm_alloc_page();
    if (!stack_page) { kfree(p); return 0; }

    p->pid        = next_pid++;
    p->state      = PROCESS_READY;
    p->stack_base = (uint64_t)stack_page;
    p->stack_top  = (uint64_t)stack_page + PAGE_SIZE;
    p->timeslice  = TIMESLICE;

    int i = 0;
    while (name[i] && i < PROCESS_NAME_MAX - 1) { p->name[i] = name[i]; i++; }

    setup_stack(p, entry);

    process_t* t = process_list;
    while (t->next != process_list) t = t->next;
    t->next = p;
    p->next = process_list;

    return p;
}

static void do_switch(void) {
    process_t* next = current_process->next;
    int loops = 0;
    while (next->state != PROCESS_READY && next != current_process) {
        next = next->next;
        if (++loops > MAX_PROCESSES) return;
    }
    if (next == current_process) return;

    process_t* old  = current_process;
    current_process = next;

    if (old->state == PROCESS_RUNNING) old->state = PROCESS_READY;
    next->state    = PROCESS_RUNNING;
    next->timeslice = TIMESLICE;   /* reset timeslice on switch-in */

    switch_context(&old->context.kernel_rsp, next->context.kernel_rsp);
}

void process_yield(void) {
    __asm__ volatile ("cli");
    current_process->timeslice = 0;   /* forfeit remaining slice */
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

    /* Preempt current process if its timeslice expired */
    if (current_process->timeslice > 0)
        current_process->timeslice--;

    if (current_process->timeslice == 0)
        do_switch();
}

void process_sleep(uint64_t ticks) {
    current_process->wake_tick = total_ticks + ticks;
    current_process->state     = PROCESS_SLEEPING;
    process_yield();
}

void process_exit(void) {
    __asm__ volatile ("cli");
    current_process->state = PROCESS_DEAD;
    do_switch();
    while (1) __asm__ volatile ("hlt");
}

process_t* process_current(void)     { return current_process; }
uint64_t   scheduler_get_ticks(void) { return total_ticks; }

process_t* process_get(uint32_t pid) {
    process_t* p = process_list;
    if (!p) return 0;
    do { if (p->pid == pid) return p; p = p->next; } while (p != process_list);
    return 0;
}
