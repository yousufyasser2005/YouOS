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
void       scheduler_tick(void);
uint64_t   scheduler_get_ticks(void);

#endif
