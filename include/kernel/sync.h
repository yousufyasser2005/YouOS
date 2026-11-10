#ifndef KERNEL_SYNC_H
#define KERNEL_SYNC_H
#include <stdint.h>

/*
 * Kernel synchronisation.
 *
 * kmutex_t is a SLEEPING, RECURSIVE mutex: a process that finds it held goes to sleep (it does not
 * spin, and the CPU runs other processes meanwhile), and the owner may lock it again (depth count),
 * so a lock-protected function can call another one.
 *
 * Holding a kmutex makes the owner un-killable until it lets go (see process_kill): killing a
 * process that holds the filesystem lock would leave the lock held forever. A process that dies by
 * itself (exit, crash) has its mutexes released by sync_process_died().
 *
 * Before multitasking exists (sched_current_pid() == 0) every call is a no-op.
 */
typedef struct kmutex {
    uint32_t       owner;      /* pid of the owner, 0 = free */
    uint32_t       depth;      /* how many times the owner has locked it */
    uint32_t       waiters;    /* processes queued for it (asleep, or woken but not yet running) */
    struct kmutex* next;       /* registry, so sync_process_died() can find locks a dead process held */
} kmutex_t;

void kmutex_init(kmutex_t* m);
void kmutex_lock(kmutex_t* m);
int  kmutex_trylock(kmutex_t* m);          /* 1 = acquired, 0 = somebody else holds it */
void kmutex_unlock(kmutex_t* m);
int  kmutex_held_by_other(const kmutex_t* m);
void sync_process_died(uint32_t pid);

/* The storage lock: serialises everything that touches the disk filesystems (YCFS, FAT16, VFS
 * descriptors) and the ATA driver's shared buffers. */
extern kmutex_t storage_lock_m;
void storage_lock(void);
void storage_unlock(void);
int  storage_busy_by_other(void);          /* true when another process holds it (best-effort callers skip work) */
void storage_lock_init(void);

#endif
