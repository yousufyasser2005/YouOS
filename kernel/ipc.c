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

/* Returns a queue's slot to the pool -- nothing called this before it
 * existed, ever, for any queue: a known, explicitly-deferred leak from
 * last session's Phase 2 (per-process I/O redirection). Each windowed
 * process (every "newterm" window) auto-creates 2 queues on first
 * touch (term_out_<pid>/term_in_<pid> -- see term_queue_name()'s
 * comment in syscall.c) and, until this, permanently consumed those 2
 * of the fixed IPC_MAX_QUEUES=16 slots for the life of the system, not
 * just the life of the process -- roughly 8 newterm windows opened
 * (and even closed again) over one boot would exhaust every slot,
 * after which ipc_create() starts failing for everyone. Called from
 * process_reap() (see windowed_ipc_queues_free() in syscall.c) once a
 * windowed process is actually dead. A no-op if `name` has no matching
 * queue -- callers don't need to know whether a queue was ever
 * actually created (e.g. a window closed before its process ever
 * wrote or read anything) before asking to free it. */
void ipc_destroy(const char* name) {
    ipc_queue_t* q = find_queue(name);
    if (!q) return;
    q->used    = 0;
    q->head    = 0;
    q->tail    = 0;
    q->count   = 0;
    q->name[0] = 0;
}

int ipc_used_count(void) {
    int n = 0;
    for (int i = 0; i < IPC_MAX_QUEUES; i++)
        if (queues[i].used) n++;
    return n;
}
