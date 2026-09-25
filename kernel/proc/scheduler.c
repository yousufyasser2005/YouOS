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

/* Idle task -- see scheduler_init()'s own comment for why it exists and
 * why it's deliberately kept OUTSIDE process_list's circular chain
 * rather than being just another process_create()'d entry. */
static process_t* idle_process    = 0;

/* Last-sampled tick counts, for scheduler_get_cpu_percent()'s delta
 * calculation (a live figure for the interval since the previous call,
 * not a since-boot average). */
static uint64_t cpu_sample_total_ticks = 0;
static uint64_t cpu_sample_idle_ticks  = 0;
static uint32_t cpu_sample_last_percent = 0;

/* Minimum ticks between real recomputes (~250ms at 100Hz). draw_stats()
 * calls scheduler_get_cpu_percent() roughly once per rendered frame, and
 * an idle desktop's frames are only ~4 ticks (~40ms) apart (bounded by
 * idle_process's own timeslice -- see do_switch()'s comment). A window
 * that short means one real-but-brief tick of work (a frame whose redraw
 * happens to straddle a timer-tick boundary) swings the reported
 * percentage by 25%+ in a single sample -- true to the tick count, but
 * not a meaningful load figure at that granularity. Requiring a longer
 * minimum window averages that out, the same way a real system monitor
 * refreshes on the order of a second rather than every single poll. */
#define CPU_SAMPLE_MIN_TICKS 25

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

/* Idle task body: nothing to do but wait for the next interrupt,
 * forever. sti+hlt (not a bare hlt) so the timer IRQ that's the whole
 * reason this ever gets scheduled again can actually fire -- same
 * atomic-enable-then-halt idiom process_sleep()'s own fallback loop
 * uses, for the same reason. */
static void idle_task_entry(void) {
    for (;;) {
        __asm__ volatile ("sti; hlt");
    }
}

void scheduler_init(void) {
    process_t* kp = (process_t*)kzalloc(sizeof(process_t));
    kp->pid   = 1;
    kp->state = PROCESS_RUNNING;
    kp->name[0]='k'; kp->name[1]='e'; kp->name[2]='r';
    kp->name[3]='n'; kp->name[4]='e'; kp->name[5]='l';
    kp->as          = kernel_as;
    /* Real syscall-entry kernel stack (TSS.RSP0 / kernel_stack_top),
     * matching process_create()'s own convention, instead of leaving
     * this at 0. Deliberately does NOT touch context.kernel_rsp above
     * -- that's pid 1's real, already-correct cooperative-switch
     * stack (whatever kernel_main()'s actual boot-time C call stack
     * is), untouched by this. Without this, do_switch() correctly
     * skips reloading TSS.RSP0/kernel_stack_top when switching back
     * to pid 1 (its old, documented behavior) -- fine as long as
     * nothing else ever ran a syscall on those globals in between,
     * but becomes a real dangling-stack bug the moment pid 1 spawns a
     * real child (sys_exec()), that child gets reaped, and pid 1
     * later makes another syscall: kernel_stack_top would still point
     * at the reaped child's freed memory. */
    {
        void* pid1_stack = kmalloc_aligned(PROCESS_STACK_SIZE, PAGE_SIZE);
        kp->stack_base = (uint64_t)pid1_stack;
        kp->stack_top  = (uint64_t)pid1_stack + PROCESS_STACK_SIZE;
    }
    kp->timeslice   = TIMESLICE;
    process_list    = kp;
    kp->next        = kp;
    current_process = kp;
    next_pid        = 2;

    /* Idle task: exists so do_switch() has something safe to fall back
     * on when no real process is READY, and so ITS tick count becomes a
     * genuine measure of idle time (scheduler_get_cpu_percent()).
     *
     * Deliberately NOT linked into process_list's circular chain like a
     * normal process_create()'d process would be. If it were, do_switch()'s
     * ordinary round-robin search would give it an equal, regular turn
     * alongside every real process -- which is wrong here: desktop.c
     * calls sys_yield() unconditionally on every single rendered frame,
     * regardless of whether it actually has nothing left to do that
     * frame, so a round-robin-fair idle task would silently steal real
     * frame time from it (visible stutter) and, worse, make "CPU%"
     * report a meaningless ~50/50 split instead of real load. Keeping it
     * outside the ring and only ever selecting it as an explicit last
     * resort (see do_switch()) means its ticks only ever accumulate when
     * truly nothing else wanted the CPU. */
    {
        idle_process = (process_t*)kzalloc(sizeof(process_t));
        idle_process->pid   = 0;
        idle_process->state = PROCESS_READY;
        idle_process->name[0]='i'; idle_process->name[1]='d';
        idle_process->name[2]='l'; idle_process->name[3]='e';
        idle_process->as    = kernel_as;
        void* idle_stack = kmalloc_aligned(PROCESS_STACK_SIZE, PAGE_SIZE);
        idle_process->stack_base = (uint64_t)idle_stack;
        idle_process->stack_top  = (uint64_t)idle_stack + PROCESS_STACK_SIZE;
        idle_process->timeslice  = TIMESLICE;
        setup_stack(idle_process, idle_task_entry);
    }
}

process_t* process_create(const char* name, void (*entry)(void),
                          address_space_t as) {
    process_t* p = (process_t*)kzalloc(sizeof(process_t));
    if (!p) return 0;

    /* Kernel stacks must come from the kernel heap (HEAP_START, PML4
     * index 256), not raw pmm_alloc_page() (PML4 index 0). Correction:
     * the low identity map (PML4[0]) is NOT excluded from address spaces
     * vmm_create_user_as() creates -- it's deep-copied, giving each
     * process its own private, independently-splittable page-directory
     * copy (see vmm.c). A raw-physical-address stack wouldn't vanish on
     * a CR3 switch, but it could end up pointing at memory whose mapping
     * has diverged from another process's own split of the same range.
     * The kernel heap range (PML4 index 256), by contrast, is a single
     * shared copy across every address space, so it's the correct place
     * for kernel stacks regardless. */
    void* stack_page = kmalloc_aligned(PROCESS_STACK_SIZE, PAGE_SIZE);
    if (!stack_page) { kfree(p); return 0; }

    p->pid        = next_pid++;
    /* process_current() is always non-null here in practice --
     * scheduler_init() (which sets it) runs long before the first
     * process_create() call anywhere in this codebase -- but guarded
     * defensively anyway, matching this file's general style. */
    p->parent_pid = process_current() ? process_current()->pid : 0;
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

/* Shared tail of a real switch decision -- state flips, CR3/TSS reload,
 * and the actual switch_context() call. Factored out of do_switch() so
 * both the "current is idle_process" and "current is a real process"
 * search paths below (which pick their `next` candidate differently)
 * share one, single-tested implementation of the switch itself. Exact
 * same logic as the pre-idle-task do_switch() body -- unchanged. */
static void switch_to(process_t* old, process_t* next) {
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
     * ITS stack, not whatever the previous process left there. The
     * stack_top == 0 guard is defensive rather than load-bearing today
     * -- every real process_t (including pid 1, since scheduler_init(),
     * and idle_process) now carries a valid syscall-entry stack -- but
     * kept in case a future process_t legitimately has none. */
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

static void do_switch(void) {
    process_t* next;

    if (current_process == idle_process) {
        /* idle_process is deliberately kept OUTSIDE process_list's
         * circular chain (see its setup comment in scheduler_init()),
         * so there's no "walked all the way back to myself" terminator
         * to rely on the way the real-process branch below has. Just
         * scan the real ring once for anything READY. */
        next = 0;
        if (process_list) {
            process_t* p = process_list;
            int loops = 0;
            do {
                if (p->state == PROCESS_READY) { next = p; break; }
                p = p->next;
            } while (++loops <= MAX_PROCESSES);
        }
        if (!next) return;   /* still nothing real to do -- stay idle */
    } else {
        /* Original round-robin search, unchanged: walk the ring from
         * current->next looking for a READY sibling, stopping either
         * when one is found or when the walk gets back to `current`
         * itself (meaning nobody else wants the CPU right now). */
        next = current_process->next;
        int loops = 0;
        while (next->state != PROCESS_READY && next != current_process) {
            next = next->next;
            if (++loops > MAX_PROCESSES) return;
        }
        if (next == current_process) {
            /* No other real process is READY. Hand off to idle_process
             * instead of just returning and leaving `current_process`
             * running uninterrupted -- idle_process is never part of
             * this ring's rotation (see its setup comment), so this is
             * the ONLY place it's ever selected. That's what makes its
             * tick count a genuine idle measurement (see
             * scheduler_get_cpu_percent()) rather than a round-robin-fair
             * share stolen from processes -- like desktop.c -- that call
             * sys_yield() every frame regardless of whether they're
             * actually out of real work to do. */
            next = idle_process;
        }
    }

    switch_to(current_process, next);
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

    /* CORRECTED, outdated comment (was written when process_create() was
     * never actually called anywhere in this codebase -- that stopped
     * being true as of the concurrency work: sys_spawn()/sys_exec() both
     * route through it via spawn_common(), and it's genuinely exercised
     * every time desktop opens a "newterm" window). With idle_process
     * now always present (see scheduler_init()/do_switch()), do_switch()
     * can NEVER return "no switch happened" the way it used to when this
     * process was the only one that existed -- it always finds either a
     * real READY sibling or idle_process to hand off to. So the
     * process_yield() call above now always performs a genuine
     * switch_context(), and control only returns here once some LATER
     * do_switch() call picks this process again -- which, by
     * scheduler_tick()'s own wake condition (total_ticks >= wake_tick),
     * can only happen once the target has already been reached.
     *
     * That makes the loop below dead in every realistic case now (its
     * condition is already false by the time we reach it) rather than
     * the load-bearing fallback it used to be. Left in place anyway, as
     * a defensive belt-and-suspenders measure -- it's correct and cheap
     * either way, and correctly re-enables interrupts (disabled for the
     * whole syscall handler: cli on entry in syscall_entry.asm, sti only
     * right before sysretq) via the same sti+hlt atomic idiom used
     * elsewhere in this file's panic/crash halt loops, so a timer IRQ
     * arriving between the check and the halt is never missed. */
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

/* Wakes whoever is BLOCKED specifically waiting for `pid` (via
 * process_wait()), if anyone -- shared by process_exit() (a process
 * ending itself) and process_kill() (one process forcibly ending
 * another) so both leave a waiting parent in the same correct state
 * rather than duplicating this search. Only one parent can
 * legitimately be waiting for a given pid, so stop at the first
 * match. */
static void wake_waiter_for(uint32_t pid) {
    if (!process_list) return;
    process_t* p = process_list;
    do {
        if (p->state == PROCESS_BLOCKED && p->waiting_for_pid == pid) {
            p->state = PROCESS_READY;
            p->waiting_for_pid = 0;
            break;
        }
        p = p->next;
    } while (p != process_list);
}

/* Reaps every zombie (PROCESS_DEAD, not yet freed) process whose real
 * parent is itself gone -- either already fully reaped (unlinked from
 * process_list entirely) or DEAD-but-not-yet-reaped itself. Never
 * touches a zombie whose parent is still alive: that's completely
 * normal, expected zombie state (e.g. a windowed child desktop.c
 * hasn't polled sys_wait_nonblock() on this exact frame yet), not an
 * orphan -- parent_pid (process.h) is what makes the distinction
 * possible at all, tracked for the first time as of this function.
 *
 * Called from every place a process can die (process_exit(),
 * process_kill(), and transitively crash.c's recovery branch via
 * process_exit()) so an orphan's own eventual death -- whenever it
 * happens, however much later -- gets swept up by whichever process
 * next happens to die anywhere in the system, rather than needing a
 * dedicated background "init reaper" task this single-core cooperative
 * scheduler has nowhere natural to run. A still-ALIVE orphan is left
 * completely alone here (nothing to reap yet, and re-parenting it to
 * pid 1 would actually break this scheme -- pid 1's own process_t is
 * never itself DEAD in normal operation, so a re-parented child would
 * stop being recognizable as an orphan the moment it later died,
 * defeating the whole mechanism); its parent_pid is deliberately left
 * pointing at its real, now-gone parent, so THIS exact check correctly
 * catches it whenever it does eventually die.
 *
 * Concrete scenario this closes: closing a WIN_PTERM window
 * (sys_kill(), see its own comment) while its shell happens to be
 * mid-"exec" of another program immediately orphans that program --
 * previously permanently unreachable and never freed (leaked pages,
 * IPC queues, everything process_reap() frees), no matter how much
 * later it exited on its own.
 *
 * current_process is explicitly never a candidate: process_exit() calls
 * this AFTER marking current_process PROCESS_DEAD but while still
 * running on ITS OWN kernel stack -- process_reap()-ing yourself mid-exit
 * would free the very stack this code is executing on. Its real parent
 * (or, if that's also gone, a LATER call to this same sweep from
 * elsewhere) reaps it once it's actually safe to. */
static void reap_orphaned_zombies(void) {
    if (!process_list) return;
    int loops = 0;
    while (loops++ < MAX_PROCESSES) {
        process_t* found = 0;
        process_t* p = process_list;
        do {
            if (p != current_process && p->state == PROCESS_DEAD) {
                process_t* parent = process_get(p->parent_pid);
                if (!parent || parent->state == PROCESS_DEAD) { found = p; break; }
            }
            p = p->next;
        } while (p != process_list);
        if (!found) return;
        process_reap(found);
    }
}

void process_exit(void) {
    __asm__ volatile ("cli");
    uint32_t my_pid = current_process->pid;
    current_process->state = PROCESS_DEAD;
    wake_waiter_for(my_pid);
    reap_orphaned_zombies();
    do_switch();
    while (1) __asm__ volatile ("hlt");
}

/* Forcibly terminates ANOTHER process -- unlike process_exit(), which
 * a process calls on ITSELF and which never returns, this is called
 * BY one process ON ANOTHER and returns normally to the caller.
 * Safe on this single-core, cooperative-plus-timer-preemption
 * scheduler: the target can never be the CURRENTLY RUNNING process
 * from the caller's own perspective, since only one process ever runs
 * at a time on one core -- by the time any code can call this at all,
 * the target is necessarily READY (sitting in the round-robin queue),
 * BLOCKED, or SLEEPING, never mid-execution. Its saved context is
 * simply abandoned exactly the way any DEAD process's already is
 * (process_exit() itself "returns" by never returning, abandoning its
 * own call stack the same way) -- do_switch()'s round-robin search
 * only ever considers PROCESS_READY processes, so a DEAD one is
 * silently skipped forever, no special unwinding needed. The caller
 * (typically whoever originally spawned it) is expected to notice via
 * sys_wait_nonblock() and reap it, same as a process that exited on
 * its own.
 *
 * Returns 0 on success, -1 if no such pid, -2 if already dead, -3 for
 * pid 1 (refused -- the boot process the whole system depends on),
 * -4 if pid is the CALLER's own pid (use process_exit() instead,
 * which correctly stops the caller from running further; this
 * function only flips a flag and returns, which would be wrong for
 * ending yourself).
 *
 * CORRECTED: this comment used to note, as a known accepted gap, that a
 * killed process's own children become permanently orphaned -- nothing
 * re-parented them or ever reaped them once they themselves exited.
 * Closed: reap_orphaned_zombies() (called below) sweeps for exactly
 * this, and the same sweep runs from process_exit() and (transitively,
 * via process_exit()) crash.c's recovery branch too, so it's covered
 * regardless of which of the three ways a process's parent can die. See
 * reap_orphaned_zombies()'s own comment for the full mechanism and the
 * concrete scenario (closing a WIN_PTERM window mid-"exec") that made
 * this a real, reachable gap rather than a theoretical one. */
int process_kill(uint32_t pid) {
    if (pid == 1) return -3;
    process_t* p = process_get(pid);
    if (!p) return -1;
    if (p == current_process) return -4;
    if (p->state == PROCESS_DEAD) return -2;
    p->state = PROCESS_DEAD;
    wake_waiter_for(pid);
    reap_orphaned_zombies();
    return 0;
}

/* Block the calling process until `child` dies, WITHOUT making it
 * compete for round-robin timeslice preemption while it waits --
 * unlike a busy `while (child->state != PROCESS_DEAD) process_yield();`
 * loop, which leaves the caller PROCESS_READY the whole time, meaning
 * do_switch() treats it as an ordinary schedulable process and the
 * timer forces a real context switch to it (CR3 switch + TLB flush)
 * roughly every TIMESLICE ticks even though it has nothing to do
 * until the child exits. Confirmed as a real, user-visible cost: the
 * boot-time desktop launch used exactly this busy-loop pattern, and
 * once desktop became a genuinely separate process_t (rather than
 * being pid 1 itself), those forced round-trips caused measurable
 * periodic stutter during continuous rendering (mouse dragging). */
void process_wait(process_t* child) {
    if (!child) return;
    process_t* self = process_current();
    while (child->state != PROCESS_DEAD) {
        self->waiting_for_pid = child->pid;
        self->state           = PROCESS_BLOCKED;
        process_yield();
        /* Resumes here once process_exit() (running as some other
         * process) wakes us by setting state back to READY and
         * do_switch() picks us again. Loop re-checks in case of a
         * spurious wake or an unrelated pid reuse edge case, rather
         * than assuming the first wake was necessarily correct. */
    }
    self->waiting_for_pid = 0;
}

process_t* process_current(void)     { return current_process; }
uint64_t   scheduler_get_ticks(void) { return total_ticks; }

/* Live "percent busy" (0-100) for the interval since the PREVIOUS call
 * to this function, not a since-boot average -- a since-boot average
 * would barely move after the system's been up a while and wouldn't
 * reflect what's actually happening right now, which is the whole point
 * of a "CPU" sidebar stat. Backed by idle_process's own real tick count
 * (see its setup comment in scheduler_init() and do_switch() for why
 * it's a genuine idle measurement and not just round-robin noise).
 * First call ever has no prior sample to diff against, so dt==0 and it
 * reports 0 rather than a meaningless full-history figure. */
uint32_t scheduler_get_cpu_percent(void) {
    uint64_t total_now = total_ticks;

    uint64_t dt = total_now - cpu_sample_total_ticks;
    if (dt < CPU_SAMPLE_MIN_TICKS) return cpu_sample_last_percent;

    uint64_t idle_now = idle_process ? idle_process->ticks : 0;
    uint64_t di        = idle_now - cpu_sample_idle_ticks;

    cpu_sample_total_ticks = total_now;
    cpu_sample_idle_ticks  = idle_now;

    if (di > dt) di = dt;   /* defensive: never report a negative busy% */
    cpu_sample_last_percent = (uint32_t)(100 - (100 * di) / dt);
    return cpu_sample_last_percent;
}

/* Exact count of process_t entries in process_list -- live processes
 * and not-yet-reaped zombies both count equally; this deliberately
 * does NOT distinguish them, since the point is to verify
 * reap_orphaned_zombies() actually frees entries (any DEAD-but-unreaped
 * entry it should have caught shows up here exactly the same as a
 * live one would). */
uint32_t scheduler_process_count(void) {
    if (!process_list) return 0;
    uint32_t n = 0;
    process_t* p = process_list;
    do { n++; p = p->next; } while (p != process_list);
    return n;
}

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

    /* Free this process's own ELF-segment and ring-3-stack backing
     * physical pages BEFORE tearing down the page tables below --
     * both need to walk/read those still-intact tables (ELF via
     * vmm_get_phys(), stack directly since it's identity-mapped) to
     * know which physical pages are theirs to free. Previously
     * neither was ever freed at all -- see vmm_destroy_user_as()'s
     * own comment for the full story; this is the other half of that
     * same fix, the "caller's job" its old comment referred to but
     * that no caller ever actually did. */
    if (child->elf_load_end > child->elf_load_base) {
        uint64_t start = child->elf_load_base & PAGE_MASK;
        uint64_t end   = PAGE_ALIGN(child->elf_load_end);
        for (uint64_t va = start; va < end; va += PAGE_SIZE) {
            uint64_t pa = vmm_get_phys(&child->as, va);
            if (pa) pmm_free_page(pa);
        }
    }
    if (child->user_stack_base) {
        uint64_t base = child->user_stack_base;
        for (uint64_t va = base; va < base + 16 * PAGE_SIZE; va += PAGE_SIZE) {
            uint64_t pa = vmm_get_phys(&child->as, va);
            if (pa) pmm_free_page(pa);
        }
    }

    /* Free the 2 IPC queues (term_out_<pid>/term_in_<pid>) a windowed
     * process's fd 0/1/2 redirection auto-created for it, if any --
     * previously nothing ever did, permanently consuming 2 of the
     * fixed IPC_MAX_QUEUES=16 slots per window for the life of the
     * system rather than the life of the process (see
     * ipc_destroy()'s own comment in ipc.c for the concrete impact).
     * Unconditional and harmless for a non-windowed child: pid never
     * had matching queues to begin with, so this just no-ops. */
    windowed_ipc_queues_free(child->pid);

    vmm_destroy_user_as(&child->as);
    kfree(child);
}
