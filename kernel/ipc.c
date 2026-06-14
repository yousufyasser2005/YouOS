#include <kernel/ipc.h>
#include <kernel/process.h>

static ipc_queue_t queues[IPC_MAX_QUEUES];

void ipc_init(void) {
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        queues[i].used  = 0;
        queues[i].head  = 0;
        queues[i].tail  = 0;
        queues[i].count = 0;
        queues[i].name[0] = 0;
    }
}

static int name_eq(const char* a, const char* b) {
    for (int i = 0; i < IPC_NAME_MAX; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0)    return 1;
    }
    return 1;
}

static ipc_queue_t* find_queue(const char* name) {
    for (int i = 0; i < IPC_MAX_QUEUES; i++)
        if (queues[i].used && name_eq(queues[i].name, name))
            return &queues[i];
    return 0;
}

int ipc_create(const char* name) {
    if (find_queue(name)) return 0; /* already exists */
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        if (!queues[i].used) {
            queues[i].used  = 1;
            queues[i].head  = 0;
            queues[i].tail  = 0;
            queues[i].count = 0;
            int k = 0;
            while (k < IPC_NAME_MAX-1 && name[k]) {
                queues[i].name[k] = name[k]; k++;
            }
            queues[i].name[k] = 0;
            return 0;
        }
    }
    return -1; /* no free slots */
}

int ipc_post(const char* name, const void* data, uint32_t len) {
    ipc_queue_t* q = find_queue(name);
    if (!q) {
        /* auto-create queue on first post */
        if (ipc_create(name) < 0) return -1;
        q = find_queue(name);
        if (!q) return -1;
    }
    if (q->count >= IPC_MAX_MSGS) return -1; /* full */
    if (len > IPC_MAX_MSGLEN) len = IPC_MAX_MSGLEN;
    ipc_msg_t* m = &q->msgs[q->tail];
    m->from_pid = process_current() ? process_current()->pid : 0;
    m->len = len;
    const uint8_t* src = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) m->data[i] = src[i];
    q->tail = (q->tail + 1) % IPC_MAX_MSGS;
    q->count++;
    return 0;
}

int ipc_recv(const char* name, void* data, uint32_t* len_out, uint32_t* from_out) {
    ipc_queue_t* q = find_queue(name);
    if (!q || q->count == 0) return -1;
    ipc_msg_t* m = &q->msgs[q->head];
    if (len_out)  *len_out  = m->len;
    if (from_out) *from_out = m->from_pid;
    uint8_t* dst = (uint8_t*)data;
    for (uint32_t i = 0; i < m->len; i++) dst[i] = m->data[i];
    q->head = (q->head + 1) % IPC_MAX_MSGS;
    q->count--;
    return 0;
}
