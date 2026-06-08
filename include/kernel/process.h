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
#define MAX_PROCESSES       16

typedef struct process {
    uint32_t         pid;
    char             name[PROCESS_NAME_MAX];
    process_state_t  state;
    cpu_context_t    context;
    uint64_t         stack_base;
    uint64_t         stack_top;
    uint64_t         ticks;
    uint64_t         wake_tick;
    uint32_t         timeslice;   /* ticks remaining in current slice */
    struct process*  next;
} process_t;

void       scheduler_init(void);
process_t* process_create(const char* name, void (*entry)(void));
void       process_yield(void);
void       process_sleep(uint64_t ticks);
void       process_exit(void);
process_t* process_current(void);
process_t* process_get(uint32_t pid);
void       scheduler_tick(void);
uint64_t   scheduler_get_ticks(void);

#endif
#define PAGE_SIZE 4096
