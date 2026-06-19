#ifndef KERNEL_SYSLOG_H
#define KERNEL_SYSLOG_H
#include <stdint.h>

void syslog_init(void);
void syslog_ready(void);
void syslog_write(const char* tag, const char* msg);
int  syslog_read(void* buf, uint32_t size);
#endif
