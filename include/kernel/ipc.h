#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H
#include <stdint.h>

#define IPC_MAX_QUEUES   16
#define IPC_MAX_MSGS     32
#define IPC_MAX_MSGLEN   128
#define IPC_NAME_MAX     32

typedef struct {
    uint32_t from_pid;
    uint32_t len;
    uint8_t  data[IPC_MAX_MSGLEN];
} ipc_msg_t;

typedef struct {
    char      name[IPC_NAME_MAX];
    ipc_msg_t msgs[IPC_MAX_MSGS];
    uint32_t  head, tail, count;
    int       used;
} ipc_queue_t;

void     ipc_init(void);
int      ipc_post(const char* name, const void* data, uint32_t len);
int      ipc_recv(const char* name, void* data, uint32_t* len, uint32_t* from);
int      ipc_create(const char* name);
#endif
