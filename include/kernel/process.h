#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/vmm.h>

typedef enum {
    PROCESS_READY    = 0,
    PROCESS_RUNNING  = 1,
    PROCESS_SLEEPING = 2,
    PROCESS_DEAD     = 3,
    PROCESS_BLOCKED  = 4,  /* Waiting on a specific child (see
                            * waiting_for_pid below) via
                            * process_wait(). Unlike READY, never
                            * selected by do_switch()'s round-robin
                            * search -- doesn't compete for timeslice
                            * preemption at all until explicitly woken
                            * by process_exit(). */
} process_state_t;

/*
 * CPU context — saved kernel stack pointer is all we need.
 * The full register state lives ON the kernel stack.
 *
 * Stack layout when a process is switched out (from IRQ):
 *
 *   [SS        ]  ← pushed by CPU on interrupt
 *   [RSP       ]
 *   [RFLAGS    ]
 *   [CS        ]
 *   [RIP       ]  ← return address (where process was interrupted)
 *   [err/0     ]  ← pushed by IRQ stub
 *   [irq_no    ]
 *   [RAX       ]  ← pushed by irq_common
 *   [RBX       ]
 *   [RCX       ]
 *   [RDX       ]
 *   [RSI       ]
 *   [RDI       ]
 *   [RBP       ]
 *   [R8..R15   ]
 *   ← RSP saved here (kernel_rsp)
 */
typedef struct {
    uint64_t kernel_rsp;    /* Saved kernel stack pointer */
} cpu_context_t;

#define PROCESS_NAME_MAX    32
#define PROCESS_STACK_SIZE  (16 * 1024)   /* 16KB kernel stack */
#define PROCESS_EXEC_ARG_SIZE 256
#define MAX_PROCESSES       16

typedef struct process {
    uint32_t         pid;
    char             name[PROCESS_NAME_MAX];
    process_state_t  state;
    cpu_context_t    context;
    address_space_t  as;          /* Address space this process runs in.
                                    * Kernel-only processes share kernel_as;
                                    * isolated processes get their own via
                                    * vmm_create_user_as(). do_switch() reloads
                                    * CR3 from this on every context switch. */
    uint64_t         stack_base;
    uint64_t         stack_top;
    uint64_t         ticks;
    uint64_t         wake_tick;
    uint32_t         timeslice;   /* ticks remaining in current slice */
    int              real_exit;    /* Set by process_create() (never by
                                     * scheduler_init(), i.e. never true for
                                     * pid 1). Tells sys_exit() to hand off
                                     * via the real scheduler (process_exit())
                                     * instead of the legacy sys_exec()
                                     * longjmp mechanism, which only pid 1's
                                     * synchronous exec chain still uses. */
    uint64_t         user_entry;    /* Ring-3 entry point. Read by
                                     * process_ring3_trampoline(); 0 for
                                     * kernel-only processes. */
    uint64_t         user_stack_top; /* Ring-3 stack top, likewise. */
    uint64_t         user_stack_base; /* Ring-3 stack base (physical ==
                                     * virtual, identity-mapped) -- 0
                                     * for kernel-only processes. Read
                                     * by process_reap() to free the
                                     * stack's backing physical pages,
                                     * which vmm_destroy_user_as() does
                                     * NOT free (it only has an
                                     * address_space_t, not this
                                     * process_t). */
    uint64_t         elf_load_base, elf_load_end; /* Virtual range
                                     * (raw, not page-aligned) of this
                                     * process's own ELF LOAD segments
                                     * -- read by process_reap() to
                                     * walk and free their backing
                                     * physical pages via
                                     * vmm_get_phys(), same reason as
                                     * user_stack_base above. Both 0
                                     * for kernel-only processes. */
    char             exec_arg[PROCESS_EXEC_ARG_SIZE]; /* Optional argument
                                     * string set by sys_exec() (e.g.
                                     * yourun's script path), read back
                                     * by the child via
                                     * sys_get_exec_arg(). Replaces the
                                     * old exec_depth-indexed static
                                     * array -- now lives directly on
                                     * the process it belongs to. */
    uint32_t         waiting_for_pid; /* Nonzero while PROCESS_BLOCKED:
                                     * the pid this process is waiting
                                     * to die. process_exit() checks
                                     * this on every exit and wakes the
                                     * matching waiter, if any. 0 = not
                                     * waiting (pid numbering starts at
                                     * 1, so 0 is safely unused). */
    int              windowed;      /* Set by sys_spawn()'s windowed
                                     * flag (Phase 2 of true concurrent
                                     * multi-program execution). When
                                     * nonzero, sys_write()/sys_read()
                                     * (fd 0/1/2) redirect through this
                                     * process's own per-pid IPC queues
                                     * (see term_queue_name() in
                                     * syscall.c) instead of the single
                                     * shared fb_terminal/keyboard
                                     * console every other process still
                                     * uses. Defaults to 0 (unwindowed)
                                     * via process_create()'s kzalloc(),
                                     * same as every other field here. */
    struct process*  next;
} process_t;

void       scheduler_init(void);
process_t* process_create(const char* name, void (*entry)(void),
                          address_space_t as);
void       process_ring3_trampoline(void);
void       process_reap(process_t* child);
void       process_wait(process_t* child); /* blocks until child dies,
                                            * without competing for
                                            * round-robin timeslice
                                            * preemption while waiting */
void       process_yield(void);
void       process_sleep(uint64_t ticks);
void       process_exit(void);
process_t* process_current(void);
process_t* process_get(uint32_t pid);
int        process_kill(uint32_t pid); /* forcibly ends another process;
                                        * see its own comment in
                                        * scheduler.c for the full
                                        * contract and return codes. */
void       scheduler_tick(void);
uint64_t   scheduler_get_ticks(void);
uint32_t   scheduler_get_cpu_percent(void); /* live "percent busy" for the
                                             * interval since the previous
                                             * call (0-100), backed by a
                                             * real idle task -- see its
                                             * own comment in scheduler.c. */

/* Frees the term_out_<pid>/term_in_<pid> IPC queues a windowed
 * process's fd 0/1/2 redirection allocated for it, if any. Implemented
 * in syscall.c (where the naming convention itself lives -- see
 * term_queue_name()'s comment there); declared here since process_reap()
 * (scheduler.c) is the caller. Safe to call for any pid, windowed or
 * not -- ipc_destroy() itself is a no-op for a name with no queue. */
void       windowed_ipc_queues_free(uint32_t pid);

/* Builds a fresh, ready-to-run child process from an initrd ELF by
 * name (path) with an optional argument string (arg) -- does NOT wait
 * on or reap it. Returns the new process_t* on success, or NULL with
 * *err set (-1 not found, -2 elf_load failed, -4 process_create
 * failed). Implemented in syscall.c (originally sys_exec()/sys_spawn()'s
 * private helper); declared here so kernel_main.c's boot-time launches
 * can share it too instead of hand-rolling the same ELF-load/stack-map/
 * process_create sequence -- see spawn_common()'s own comment in
 * syscall.c for why that mattered (a real, if minor, leak). */
process_t* spawn_common(const char* path, const char* arg, uint64_t* err);

#endif
