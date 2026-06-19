#ifndef KERNEL_CRASH_H
#define KERNEL_CRASH_H
#include <stdint.h>
#include <kernel/idt.h>

#define CRASH_LOG_LBA  1
#define CRASH_MAGIC    0xC4A51234U
#define CRASH_MAX_PREV 3

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t crash_count;
    uint64_t ticks;
    uint32_t exc_no;
    uint32_t ring;
    uint64_t rip, rsp, rbp, cr2, err_code;
    char     proc_name[32];
    char     exc_name[32];
    uint8_t  recovered;
    uint8_t  pad[3];
    char     prev[CRASH_MAX_PREV][64];
} crash_log_t;

void crash_init(void);
void crash_handle(registers_t* regs);
int  crash_read(void* buf, uint32_t size);
#endif
