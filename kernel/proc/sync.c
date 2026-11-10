#include <kernel/sync.h>
#include <kernel/process.h>   /* sched_* declarations */

static kmutex_t* registry = 0;
kmutex_t storage_lock_m;

/* Interrupts are masked around every state change: the kernel can be interrupted (timer) wherever
 * interrupts are on, and the owner/depth update must be atomic with respect to that. */
#ifdef SYNC_HOST_TEST          /* host test build: no privileged instructions */
static inline uint64_t irq_save(void) { return 0; }
static inline void irq_restore(uint64_t f) { (void)f; }
#else
static inline uint64_t irq_save(void) {
    uint64_t f;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f) : : "memory");
    return f;
}
static inline void irq_restore(uint64_t f) {
    __asm__ volatile ("pushq %0; popfq" : : "r"(f) : "memory", "cc");
}
#endif

void kmutex_init(kmutex_t* m) {
    uint64_t f = irq_save();
    m->owner = 0; m->depth = 0; m->waiters = 0; m->next = registry; registry = m;
    irq_restore(f);
}

void kmutex_lock(kmutex_t* m) {
    uint32_t me = sched_current_pid();
    if (!me) return;                                   /* multitasking not running: nothing to protect against */
    uint64_t f = irq_save();
    if (m->owner == me) { m->depth++; irq_restore(f); return; }
    /* Wait while it is held. If it is FREE but other processes are already queued for it (the unlock
     * woke them and they have not run yet), let them go first. Without this the process that just
     * unlocked takes the lock straight back on its next call and the sleepers starve: seen on real
     * runs as sys_stat blocking for 4.6 s behind three busy writers. */
    int queued = 0;
    while (m->owner || (m->waiters && !queued)) {
        if (m->owner) { if (!queued) { m->waiters++; queued = 1; } sched_block(m, 0); }
        else sched_yield_kernel();
    }
    if (queued) m->waiters--;
    m->owner = me; m->depth = 1;
    sched_nointr_enter();                              /* the owner may not be killed while it holds the lock */
    irq_restore(f);
}

int kmutex_trylock(kmutex_t* m) {
    uint32_t me = sched_current_pid();
    if (!me) return 1;
    uint64_t f = irq_save();
    int ok = 1;
    if (m->owner == me) m->depth++;
    else if (m->owner) ok = 0;
    else { m->owner = me; m->depth = 1; sched_nointr_enter(); }
    irq_restore(f);
    return ok;
}

void kmutex_unlock(kmutex_t* m) {
    uint32_t me = sched_current_pid();
    if (!me) return;
    uint64_t f = irq_save();
    if (m->owner != me) { irq_restore(f); return; }    /* not ours (e.g. lock was taken before multitasking) */
    if (--m->depth == 0) {
        m->owner = 0;
        sched_wake(m);
        irq_restore(f);
        sched_nointr_leave();                          /* a kill that arrived meanwhile happens now */
        return;
    }
    irq_restore(f);
}

int kmutex_held_by_other(const kmutex_t* m) {
    uint32_t me = sched_current_pid();
    return me && m->owner && m->owner != me;
}

/* A process is gone (exit, crash, forced kill): release whatever kernel locks it still owned, so
 * nobody waits forever on a lock whose owner no longer exists. */
void sync_process_died(uint32_t pid) {
    uint64_t f = irq_save();
    for (kmutex_t* m = registry; m; m = m->next) {
        if (m->owner == pid) { m->owner = 0; m->depth = 0; sched_wake(m); }
    }
    irq_restore(f);
}

void storage_lock_init(void) { kmutex_init(&storage_lock_m); }
void storage_lock(void)      { kmutex_lock(&storage_lock_m); }
void storage_unlock(void)    { kmutex_unlock(&storage_lock_m); }
int  storage_busy_by_other(void) { return kmutex_held_by_other(&storage_lock_m); }
