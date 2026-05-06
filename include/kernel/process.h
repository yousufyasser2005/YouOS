#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdint.h>
#include <stddef.h>

/*
 * Process states
 */
typedef enum {
    PROCESS_READY    = 0,   /* Ready to run         */
    PROCESS_RUNNING  = 1,   /* Currently running    */
    PROCESS_SLEEPING = 2,   /* Waiting for event    */
    PROCESS_DEAD     = 3,   /* Terminated           */
} process_state_t;

/*
 * CPU context — all registers saved during context switch
 */
typedef struct {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9,  r8;
    uint64_t rbp, rdi, rsi, rdx;
    uint64_t rcx, rbx, rax;
    uint64_t rip;       /* Instruction pointer  */
    uint64_t cs;        /* Code segment         */
    uint64_t rflags;    /* CPU flags            */
    uint64_t rsp;       /* Stack pointer        */
    uint64_t ss;        /* Stack segment        */
} __attribute__((packed)) cpu_context_t;

/*
 * Process Control Block (PCB)
 */
#define PROCESS_NAME_MAX    32
#define PROCESS_STACK_SIZE  (16 * 1024)   /* 16KB stack per process */
#define MAX_PROCESSES       16

typedef struct process {
    uint32_t         pid;                       /* Process ID           */
    char             name[PROCESS_NAME_MAX];    /* Process name         */
    process_state_t  state;                     /* Current state        */
    cpu_context_t    context;                   /* Saved CPU context    */
    uint64_t         stack_base;                /* Stack memory base    */
    uint64_t         stack_top;                 /* Stack top (initial)  */
    uint64_t         ticks;                     /* CPU ticks used       */
    uint64_t         wake_tick;                 /* Wake up at this tick */
    struct process*  next;                      /* Next in run queue    */
} process_t;

/* Initialize the scheduler */
void scheduler_init(void);

/* Create a new kernel process */
process_t* process_create(const char* name, void (*entry)(void));

/* Yield CPU to next process */
void process_yield(void);

/* Sleep for n timer ticks */
void process_sleep(uint64_t ticks);

/* Exit current process */
void process_exit(void);

/* Get current running process */
process_t* process_current(void);

/* Called by timer IRQ — picks next process */
void scheduler_tick(void);

/* Get process by PID */
process_t* process_get(uint32_t pid);

#endif /* KERNEL_PROCESS_H */

/* Check and perform deferred context switch */
void scheduler_check(void);
