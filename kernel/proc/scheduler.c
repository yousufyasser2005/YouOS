#include <kernel/process.h>
#include <kernel/heap.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>
#include <kernel/vmm.h>

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
    kp->as          = kernel_as;
    kp->stack_base  = 0;
    kp->stack_top   = 0;
    kp->timeslice   = TIMESLICE;
    process_list    = kp;
    kp->next        = kp;
    current_process = kp;
    next_pid        = 2;
}

process_t* process_create(const char* name, void (*entry)(void),
                          address_space_t as) {
    process_t* p = (process_t*)kzalloc(sizeof(process_t));
    if (!p) return 0;

    /* Kernel stacks must come from the kernel heap (HEAP_START, PML4
     * index 256), not raw pmm_alloc_page() (PML4 index 0). The low
     * identity map is deliberately excluded from every address space
     * vmm_create_user_as() creates -- a stack allocated from it would
     * vanish from the page tables the instant CR3 switches to a real
     * process address space, including the one currently executing
     * on it. The kernel heap range is copied into every address
     * space, so it stays valid across any CR3 switch. */
    void* stack_page = kmalloc_aligned(PROCESS_STACK_SIZE, PAGE_SIZE);
    if (!stack_page) { kfree(p); return 0; }

    p->pid        = next_pid++;
    p->state      = PROCESS_READY;
    p->as         = as;
    p->real_exit  = 1;
    p->stack_base = (uint64_t)stack_page;
    p->stack_top  = (uint64_t)stack_page + PROCESS_STACK_SIZE;
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

    /* Reload CR3 only if the incoming process actually uses a
     * different address space -- avoids a needless full TLB flush
     * when switching between two processes that share kernel_as
     * (true for every process today; will matter once real
     * per-process user address spaces exist). */
    if (next->as.pml4_phys != old->as.pml4_phys) {
        vmm_switch(&next->as);
    }

    /* Reload TSS.RSP0 to the incoming process's own kernel stack, so a
     * maskable interrupt (the timer, now that ring-3 runs with IF=1)
     * firing while this process executes ring-3 code lands safely on
     * ITS stack, not whatever the previous process left there. Skip
     * processes with no dedicated kernel stack of their own
     * (stack_top == 0 -- true of pid 1, which owns the original
     * boot-time kernel stack rather than one process_create()
     * allocated, and isn't switched to via this path in the same way
     * a real spawned child is). */
    if (next->stack_top != 0) {
        extern void tss_set_kernel_stack(uint64_t);
        tss_set_kernel_stack(next->stack_top);

        /* syscall_entry.asm uses a SEPARATE global (kernel_stack_top,
         * declared in syscall.c) for SYSCALL/SYSRET-driven ring3->ring0
         * entry -- it does NOT consult TSS.RSP0 at all (that's only for
         * IDT-gate/interrupt-driven entry, e.g. the timer or a fault).
         * Both mechanisms need to point at the SAME incoming process's
         * kernel stack, or a syscall issued by this process would run
         * on whatever stack this global last held (confirmed as the
         * root cause of a real crash: a process_create()-spawned
         * child's first syscall corrupted memory and eventually
         * crashed with an unrelated-looking Invalid Opcode fault,
         * because this global was still 0 -- syscall_init() hadn't
         * even run yet at the point the child was created). */
        extern uint64_t kernel_stack_top;
        kernel_stack_top = next->stack_top;
    }

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
    uint64_t target = total_ticks + ticks;
    current_process->wake_tick = target;
    current_process->state     = PROCESS_SLEEPING;
    process_yield();

    /* If do_switch() found another PROCESS_READY process to hand off to,
     * control returns here only once scheduler_tick() has observed
     * total_ticks >= wake_tick and flipped us back to READY/RUNNING --
     * i.e. we really did sleep the requested duration cooperatively, and
     * the loop below is a no-op (condition already false).
     *
     * But today, process_create() -- the only way a second process_t
     * ever enters process_list -- is never actually called anywhere in
     * this codebase (confirmed by a full-tree grep). Every real program
     * (shell commands, desktop, mpy via yourun, nested execs) runs via
     * sys_exec()'s same-context nested call or the boot-time
     * jump_to_userspace excursion, never as a second scheduler entry.
     * So in practice do_switch() always finds itself as the only
     * candidate and returns immediately without switching or waiting at
     * all -- process_yield() above returns right back here with no real
     * time having passed, current_process->state left at SLEEPING even
     * though this process never actually stopped running.
     *
     * Detect that case (target not yet reached) and fall back to
     * directly halting the CPU until real time elapses, using the timer
     * interrupt that's already firing regardless of whether the
     * scheduler's multi-process machinery is ever wired up. Interrupts
     * are disabled for the whole syscall handler (cli on entry in
     * syscall_entry.asm, sti only right before sysretq at the very end)
     * so they must be explicitly re-enabled here or the timer IRQ could
     * never fire and hlt would wait forever -- sti+hlt together is the
     * standard atomic idiom to avoid missing an interrupt that arrives
     * between the check and the halt, already used elsewhere in this
     * file's panic/crash halt loops. total_ticks is read through a
     * volatile pointer here since it's not declared volatile itself --
     * without that, the compiler could legally cache the read across
     * loop iterations and never observe the IRQ handler's update. */
    volatile uint64_t* vt = &total_ticks;
    while (*vt < target) {
        __asm__ volatile ("sti; hlt");
    }
    current_process->state = PROCESS_RUNNING;
}

/* Generic entry point for a process_t that should launch straight into
 * a ring-3 program. Callers set user_entry/user_stack_top on the
 * returned process_t BEFORE it's ever scheduled (it starts life
 * PROCESS_READY, not RUNNING, so there's no race with do_switch()
 * picking it up early) and pass this function as process_create()'s
 * 'entry' parameter. jump_to_userspace() never returns in the normal
 * sense -- control only comes back to kernel context later via a
 * syscall or fault, never back to this call site. */
void process_ring3_trampoline(void) {
    process_t* p = process_current();
    extern void jump_to_userspace(uint64_t entry, uint64_t stack_top);
    jump_to_userspace(p->user_entry, p->user_stack_top);
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

/* Unlink a process_t from the circular process_list. Handles the
* head-of-list case for correctness even though nothing currently
* reaps pid 1 (process_list always points at it in practice, since
* it's the only process scheduler_init() ever creates directly) --
* not relying on that staying true forever. */
static void process_unlink(process_t* victim) {
    if (!process_list) return;
    if (process_list == victim) {
        if (victim->next == victim) { process_list = 0; return; }
        process_t* tail = process_list;
        while (tail->next != process_list) tail = tail->next;
        process_list = victim->next;
        tail->next   = process_list;
        return;
    }
    process_t* p = process_list;
    do {
        if (p->next == victim) { p->next = victim->next; return; }
        p = p->next;
    } while (p != process_list);
}

/* Free a dead process's resources: unlink it from process_list, free
* its kernel stack (kmalloc_aligned()-backed, per process_create()),
* destroy its address space (vmm_destroy_user_as() already safely
* no-ops for a shared kernel_as rather than a real per-process one),
* then free the process_t itself.
*
* SAFETY: only call this once the caller has observed
* child->state == PROCESS_DEAD. By that point do_switch() has
* already completed a real switch_context() away from this process
* (that's the only way its state could have become visible as DEAD
* to anything else) so its kernel stack is definitely not in use by
* the CPU anymore -- freeing it is safe, not a use-after-free of a
* still-running context. This function itself does NOT check for
* PROCESS_DEAD beyond a defensive guard, since the real safety
* invariant is "has anything actually observed this transition",
* which only the caller can know. */
void process_reap(process_t* child) {
    if (!child) return;
    if (child->state != PROCESS_DEAD) return;
    process_unlink(child);
    if (child->stack_base) kfree_aligned((void*)child->stack_base);
    vmm_destroy_user_as(&child->as);
    kfree(child);
}
