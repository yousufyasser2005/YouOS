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

/* Frees a queue by name, returning its slot to the fixed-size
 * IPC_MAX_QUEUES pool. Safe to call on a name with no matching queue
 * (no-op) -- see its own comment in ipc.c. */
void     ipc_destroy(const char* name);

/* Number of currently-used queue slots out of IPC_MAX_QUEUES -- exists
 * so the queue-leak fix (ipc_destroy(), above) can actually be observed
 * rather than trusted, same reasoning as sys_meminfo() for the
 * process-reap leak fix last session. See sys_ipcinfo()'s comment in
 * syscall.c. */
int      ipc_used_count(void);
#endif
