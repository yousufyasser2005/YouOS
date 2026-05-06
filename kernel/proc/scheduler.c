#include <kernel/process.h>
#include <kernel/heap.h>
#include <kernel/vga.h>

static process_t*  current_process = 0;
static process_t*  process_list    = 0;
static uint32_t    next_pid        = 1;
static uint64_t    total_ticks     = 0;
static process_t*  idle_process    = 0;

/* Flag set by timer IRQ — checked at safe points */
static volatile int need_switch = 0;

extern void context_switch(cpu_context_t* old_ctx, cpu_context_t* new_ctx);

static void enqueue(process_t* p) {
    if (!process_list) { process_list = p; p->next = p; return; }
    process_t* tail = process_list;
    while (tail->next != process_list) tail = tail->next;
    tail->next = p; p->next = process_list;
}

static process_t* find_next_ready(void) {
    if (!process_list) return idle_process;
    process_t* p = current_process->next;
    process_t* start = p;
    do {
        if (p->state == PROCESS_SLEEPING && total_ticks >= p->wake_tick)
            p->state = PROCESS_READY;
        if (p->state == PROCESS_READY) return p;
        p = p->next;
    } while (p != start);
    return idle_process;
}

static void idle_task(void) {
    while (1) {
        __asm__ volatile ("sti; hlt");
    }
}

static process_t* make_process(const char* name, void (*entry)(void),
                                uint32_t pid, process_state_t state)
{
    process_t* p = (process_t*)kzalloc(sizeof(process_t));
    if (!p) return 0;
    p->pid   = pid ? pid : next_pid++;
    p->state = state;
    int i = 0;
    while (name[i] && i < PROCESS_NAME_MAX-1) { p->name[i]=name[i]; i++; }
    p->name[i] = 0;
    if (entry) {
        p->stack_base = (uint64_t)kzalloc(PROCESS_STACK_SIZE);
        p->stack_top  = p->stack_base + PROCESS_STACK_SIZE;
        uint64_t rsp  = p->stack_top & ~(uint64_t)0xF;
        rsp -= 8;
        *(uint64_t*)rsp       = (uint64_t)process_exit;
        p->context.rip        = (uint64_t)entry;
        p->context.rsp        = rsp;
        p->context.rflags     = 0x202;
        p->context.cs         = 0x08;
        p->context.ss         = 0x10;
        p->context.rbp        = rsp;
    }
    return p;
}

process_t* process_create(const char* name, void (*entry)(void)) {
    process_t* p = make_process(name, entry, 0, PROCESS_READY);
    if (p) enqueue(p);
    return p;
}

void scheduler_init(void) {
    idle_process = make_process("idle", idle_task, 0, PROCESS_READY);
    process_t* kp = make_process("kernel", 0, 1, PROCESS_RUNNING);
    enqueue(kp);
    enqueue(idle_process);
    current_process = kp;
    next_pid = 2;
}

static void do_switch(void) {
    process_t* next = find_next_ready();
    if (!next || next == current_process) return;
    process_t* old  = current_process;
    current_process = next;
    if (old->state == PROCESS_RUNNING) old->state = PROCESS_READY;
    next->state = PROCESS_RUNNING;
    context_switch(&old->context, &next->context);
}

/*
 * scheduler_tick — called from timer IRQ
 * Updates ticks and wakes sleeping processes.
 * Sets need_switch flag every 10 ticks (10ms preemption window).
 */
void scheduler_tick(void) {
    total_ticks++;
    if (!process_list) return;

    /* Wake sleeping processes */
    process_t* p = process_list;
    do {
        if (p->state == PROCESS_SLEEPING && total_ticks >= p->wake_tick)
            p->state = PROCESS_READY;
        p = p->next;
    } while (p != process_list);

    /* Signal that a switch should happen */
    if (total_ticks % 10 == 0) need_switch = 1;
}

/*
 * scheduler_check — call this at safe points (not in IRQ handler)
 * to do a deferred context switch
 */
void scheduler_check(void) {
    if (need_switch) {
        need_switch = 0;
        do_switch();
    }
}

void process_yield(void) {
    __asm__ volatile ("cli");
    need_switch = 0;
    do_switch();
    __asm__ volatile ("sti");
}

void process_sleep(uint64_t ticks) {
    __asm__ volatile ("cli");
    current_process->state     = PROCESS_SLEEPING;
    current_process->wake_tick = total_ticks + ticks;
    need_switch = 0;
    do_switch();
    __asm__ volatile ("sti");
}

void process_exit(void) {
    __asm__ volatile ("cli");
    current_process->state = PROCESS_DEAD;
    do_switch();
    __asm__ volatile ("sti");
    while (1) __asm__ volatile ("hlt");
}

process_t* process_current(void) { return current_process; }

process_t* process_get(uint32_t pid) {
    process_t* p = process_list;
    if (!p) return 0;
    do { if (p->pid==pid) return p; p=p->next; } while(p!=process_list);
    return 0;
}

uint64_t scheduler_get_ticks(void) { return total_ticks; }
