#ifndef KERNEL_USERSPACE_H
#define KERNEL_USERSPACE_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/vmm.h>

#define USER_STACK_TOP   0x7FFFF000ULL
#define USER_STACK_PAGES 4

typedef struct {
    address_space_t as;
    uint64_t        entry;
    uint64_t        stack_top;
    uint64_t        code_phys;
    size_t          code_size;
} user_process_t;

user_process_t* user_process_create(const char* name, void (*entry)(void));
void            user_process_destroy(user_process_t* proc);
void            user_process_exec(user_process_t* proc);

#endif
