#ifndef KERNEL_KJMP_H
#define KERNEL_KJMP_H
#include <stdint.h>

typedef struct {
    uint64_t rsp, rbp, rbx, r12, r13, r14, r15, rip;
} kjmp_buf_t;

/* Returns 0 on save, 1 on restore */
int  ksetjmp(kjmp_buf_t* buf);
void klongjmp(kjmp_buf_t* buf) __attribute__((noreturn));

#endif
