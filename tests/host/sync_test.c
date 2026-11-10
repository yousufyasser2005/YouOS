/* Host test for kernel/proc/sync.c (the real file) against a SIMULATED scheduler: processes are
 * ucontext coroutines, sched_block()/sched_wake()/nointr/deferred-kill follow the same rules as
 * kernel/proc/scheduler.c. Checks the lock logic, not the context-switch assembly. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <kernel/sync.h>
#include <kernel/process.h>

static int failures = 0, checks = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

#define MAXT 8
enum { T_FREE, T_READY, T_BLOCKED, T_DEAD };
typedef struct {
    ucontext_t ctx; char* stack; int state; void* chan; long deadline; int timedout;
    uint32_t nointr; int kill_pending; uint32_t pid; void (*fn)(void*); void* arg; long runs;
} task_t;
static task_t T[MAXT]; static int ntasks, cur = -1; static ucontext_t sched_ctx; static long vclock; static int deadlock;

uint32_t sched_current_pid(void) { return cur >= 0 ? T[cur].pid : 0; }
static void back_to_sched(void) { swapcontext(&T[cur].ctx, &sched_ctx); }
static void task_exit_now(void) { sync_process_died(T[cur].pid); T[cur].state = T_DEAD; back_to_sched(); abort(); }
int sched_block(void* chan, uint64_t timeout) {
    task_t* t = &T[cur];
    t->nointr++; t->state = T_BLOCKED; t->chan = chan; t->timedout = 0; t->deadline = timeout ? vclock + (long)timeout : 0;
    back_to_sched();
    t->nointr--; t->chan = 0; t->deadline = 0;
    return t->timedout;
}
void sched_wake(void* chan) { for (int i = 0; i < ntasks; i++) if (T[i].state == T_BLOCKED && T[i].chan == chan) { T[i].state = T_READY; T[i].chan = 0; } }
void sched_nointr_enter(void) { T[cur].nointr++; }
void sched_nointr_leave(void) { if (T[cur].nointr) T[cur].nointr--; if (T[cur].nointr == 0 && T[cur].kill_pending) { T[cur].kill_pending = 0; task_exit_now(); } }
static int kill_task(uint32_t pid) {
    for (int i = 0; i < ntasks; i++) if (T[i].pid == pid && T[i].state != T_DEAD) {
        if (T[i].nointr) { T[i].kill_pending = 1; return 1; }       /* deferred, like process_kill() */
        T[i].state = T_DEAD; sync_process_died(pid); return 0;
    }
    return -1;
}
static void task_yield(void) { T[cur].state = T_READY; back_to_sched(); }
void sched_yield_kernel(void) { task_yield(); }

static void tramp(void) { T[cur].fn(T[cur].arg); T[cur].state = T_DEAD; back_to_sched(); }
static int spawn(void (*fn)(void*), void* arg) {
    task_t* t = &T[ntasks]; memset(t, 0, sizeof *t);
    t->stack = malloc(256 * 1024); t->pid = (uint32_t)(ntasks + 10); t->fn = fn; t->arg = arg; t->state = T_READY;
    getcontext(&t->ctx); t->ctx.uc_stack.ss_sp = t->stack; t->ctx.uc_stack.ss_size = 256 * 1024; t->ctx.uc_link = 0;
    makecontext(&t->ctx, tramp, 0);
    return ntasks++;
}
static void run_all(void) {
    int last = -1;
    for (;;) {
        int next = -1;
        for (int k = 1; k <= ntasks; k++) { int i = (last + k) % ntasks; if (T[i].state == T_READY) { next = i; break; } }
        if (next < 0) {                                          /* nobody ready: let time pass for timed sleepers */
            long best = -1; int bi = -1, alive = 0;
            for (int i = 0; i < ntasks; i++) if (T[i].state == T_BLOCKED) { alive++; if (T[i].deadline && (best < 0 || T[i].deadline < best)) { best = T[i].deadline; bi = i; } }
            if (!alive) break;
            if (bi < 0) { deadlock = 1; break; }
            vclock = best; T[bi].state = T_READY; T[bi].timedout = 1; T[bi].chan = 0; continue;
        }
        cur = next; last = next; T[next].runs++;
        swapcontext(&sched_ctx, &T[next].ctx);
        cur = -1;
    }
}
static void reset(void) { for (int i = 0; i < ntasks; i++) free(T[i].stack); memset(T, 0, sizeof T); ntasks = 0; cur = -1; vclock = 0; deadlock = 0; }

static kmutex_t M;
static int inside, max_inside, done_count;

/* ---- 1. mutual exclusion, with sleeping inside the critical section ---- */
static void worker(void* a) {
    (void)a;
    for (int i = 0; i < 60; i++) {
        kmutex_lock(&M);
        inside++; if (inside > max_inside) max_inside = inside;
        sched_block(&inside, 1);                       /* sleeps while holding the lock, like a disk wait */
        task_yield();
        inside--;
        kmutex_unlock(&M);
        task_yield();
    }
    done_count++;
}
/* ---- 2/3. recursion + no busy waiting ---- */
static int b_entered, b_runs_while_waiting;
static void holder(void* a) {
    (void)a;
    kmutex_lock(&M); kmutex_lock(&M);                  /* depth 2 */
    for (int i = 0; i < 30; i++) task_yield();
    kmutex_unlock(&M);                                 /* depth 1: still owned */
    for (int i = 0; i < 30; i++) task_yield();
    expect(!b_entered, "recursive lock: inner unlock keeps the lock; the waiter has not entered");
    b_runs_while_waiting = (int)T[1].runs;
    kmutex_unlock(&M);                                 /* depth 0: waiter wakes */
}
static void waiter(void* a) { (void)a; kmutex_lock(&M); b_entered = 1; kmutex_unlock(&M); }
/* ---- 4. owner dies holding the lock ---- */
static int got_after_death;
static void dying(void* a) { (void)a; kmutex_lock(&M); task_yield(); task_exit_now(); }
static void survivor(void* a) { (void)a; kmutex_lock(&M); got_after_death = 1; kmutex_unlock(&M); }
/* ---- 5. kill while inside the critical section ---- */
static int cs_finished, ran_after_unlock, other_got_lock;
static void victim(void* a) {
    (void)a; kmutex_lock(&M);
    for (int i = 0; i < 5; i++) task_yield();
    cs_finished = 1;
    kmutex_unlock(&M);                                 /* the deferred kill takes effect here */
    ran_after_unlock = 1;
}
static void killer(void* a) { (void)a; task_yield(); task_yield(); kill_task(T[0].pid); }
static void bystander(void* a) { (void)a; for (int i = 0; i < 3; i++) task_yield(); kmutex_lock(&M); other_got_lock = 1; kmutex_unlock(&M); }
/* ---- 6. kill a task that is waiting for the lock ---- */
static int w_got, w_after;
static void lock_holder(void* a) { (void)a; kmutex_lock(&M); for (int i = 0; i < 10; i++) task_yield(); kmutex_unlock(&M); }
static void lock_waiter(void* a) { (void)a; kmutex_lock(&M); w_got = 1; kmutex_unlock(&M); w_after = 1; }
static void waiter_killer(void* a) { (void)a; for (int i = 0; i < 4; i++) task_yield(); kill_task(T[1].pid); }
/* ---- 7. trylock ---- */
static int try_a, try_b, busy_other, busy_self;
static void try_holder(void* a) { (void)a; kmutex_lock(&M); for (int i = 0; i < 6; i++) task_yield(); kmutex_unlock(&M); }
static void try_other(void* a) { (void)a; task_yield(); try_a = kmutex_trylock(&M); busy_other = kmutex_held_by_other(&M); for (int i = 0; i < 12; i++) task_yield(); try_b = kmutex_trylock(&M); if (try_b) { busy_self = kmutex_held_by_other(&M); kmutex_unlock(&M); } }

/* ---- 8. no starvation: a process that locks and unlocks in a tight loop must not keep a waiter out ---- */
static int h_iter, w_got_at, w_done;
static void hammer(void* a) { (void)a; for (h_iter = 0; h_iter < 40; h_iter++) { kmutex_lock(&M); sched_block(&inside, 1); kmutex_unlock(&M); } }
static void late_waiter(void* a) { (void)a; task_yield(); task_yield(); kmutex_lock(&M); w_got_at = h_iter; w_done = 1; kmutex_unlock(&M); }
/* three hammering processes + one that needs the lock only occasionally: how many critical sections
 * of the others run before it gets in? (the real symptom: sys_stat waiting seconds behind 3 writers) */
static int busy_done[3], occ_wait_cs, occ_got;
static void hammer3(void* a) { int id = (int)(long)a; for (int i = 0; i < 40; i++) { kmutex_lock(&M); sched_block(&inside, 1); busy_done[id]++; kmutex_unlock(&M); } }
static void occasional(void* a) {
    (void)a; task_yield(); task_yield();
    int before = busy_done[0] + busy_done[1] + busy_done[2];
    kmutex_lock(&M);
    occ_wait_cs = busy_done[0] + busy_done[1] + busy_done[2] - before; occ_got = 1;
    kmutex_unlock(&M);
}

int main(void) {
    kmutex_init(&M);
    printf("== kernel mutex (real sync.c, simulated scheduler)\n");

    /* before multitasking: everything is a harmless no-op */
    kmutex_lock(&M); kmutex_unlock(&M);
    expect(M.owner == 0 && M.depth == 0 && kmutex_trylock(&M) == 1, "no multitasking: lock/unlock are no-ops and never mark the lock owned");

    reset(); inside = max_inside = done_count = 0;
    for (int i = 0; i < 4; i++) spawn(worker, 0);
    run_all();
    expect(!deadlock && done_count == 4, "4 processes x 60 critical sections, each sleeping inside the lock: all finish, no deadlock");
    expect(max_inside == 1, "never two processes inside the critical section at once");
    expect(M.owner == 0 && M.depth == 0, "lock is free at the end");

    reset(); b_entered = 0;
    spawn(holder, 0); spawn(waiter, 0);
    run_all();
    expect(!deadlock && b_entered, "recursive lock: after the outer unlock the waiter gets in");
    expect(b_runs_while_waiting <= 3, "a sleeping waiter is not scheduled while the lock is held (no busy waiting)");

    reset(); got_after_death = 0;
    spawn(dying, 0); spawn(survivor, 0);
    run_all();
    expect(!deadlock && got_after_death && M.owner == 0, "owner dies holding the lock: it is released, the next process gets it");

    reset(); cs_finished = ran_after_unlock = other_got_lock = 0;
    spawn(victim, 0); spawn(killer, 0); spawn(bystander, 0);
    run_all();
    expect(cs_finished, "kill during the critical section: the process finishes the section first");
    expect(!ran_after_unlock && T[0].state == T_DEAD, "...and dies the moment it unlocks (no code runs after)");
    expect(other_got_lock && !deadlock && M.owner == 0, "...and the lock is released for everyone else");

    reset(); w_got = w_after = 0;
    spawn(lock_holder, 0); spawn(lock_waiter, 0); spawn(waiter_killer, 0);
    run_all();
    expect(!deadlock && M.owner == 0, "kill of a process waiting for the lock: no deadlock, lock not leaked");
    expect(T[1].state == T_DEAD && !w_after, "...it is dead and did not run on after its critical section");

    reset(); try_a = try_b = busy_other = busy_self = -1;
    spawn(try_holder, 0); spawn(try_other, 0);
    run_all();
    expect(try_a == 0 && busy_other == 1, "trylock fails while another process holds it; held_by_other reports it");
    expect(try_b == 1 && busy_self == 0, "trylock succeeds once it is free; held_by_other is false for the owner");

    reset(); h_iter = w_got_at = w_done = -1; w_done = 0;
    spawn(hammer, 0); spawn(late_waiter, 0);
    run_all();
    expect(!deadlock && w_done, "waiter behind a process that relocks immediately: it does get the lock");
    expect(w_got_at >= 0 && w_got_at <= 3, "...within a few of its critical sections, not after it has finished all 40 (no starvation)");

    reset(); occ_wait_cs = -1; occ_got = 0; memset(busy_done, 0, sizeof busy_done);
    spawn(hammer3, (void*)0); spawn(hammer3, (void*)1); spawn(hammer3, (void*)2); spawn(occasional, 0);
    run_all();
    expect(!deadlock && occ_got && M.owner == 0 && M.waiters == 0, "3 busy processes + 1 occasional: all finish, nobody left queued");
    expect(occ_wait_cs >= 0 && occ_wait_cs <= 6, "the occasional process waits for at most a handful of others' critical sections (not for all 120)");

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
