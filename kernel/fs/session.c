#include <kernel/session.h>

#define MAX_SESSION_UIDS 16

typedef struct {
    uint64_t cr3;
    uint32_t uid;
    uint32_t gid;
    int      valid;
} session_entry_t;

static session_entry_t sessions[MAX_SESSION_UIDS];

static uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void session_set_uid(uint32_t uid, uint32_t gid) {
    uint64_t cr3 = read_cr3();
    for (int i = 0; i < MAX_SESSION_UIDS; i++) {
        if (sessions[i].valid && sessions[i].cr3 == cr3) {
            sessions[i].uid = uid;
            sessions[i].gid = gid;
            return;
        }
    }
    for (int i = 0; i < MAX_SESSION_UIDS; i++) {
        if (!sessions[i].valid) {
            sessions[i].valid = 1;
            sessions[i].cr3   = cr3;
            sessions[i].uid   = uid;
            sessions[i].gid   = gid;
            return;
        }
    }
    /* Table full — shouldn't happen at MAX_SESSION_UIDS=16 with realistically
     * one or two live CR3s (desktop + occasional shell); dropping silently
     * beats crashing the kernel. */
}

int session_lookup(uint32_t* uid_out, uint32_t* gid_out) {
    uint64_t cr3 = read_cr3();
    for (int i = 0; i < MAX_SESSION_UIDS; i++) {
        if (sessions[i].valid && sessions[i].cr3 == cr3) {
            *uid_out = sessions[i].uid;
            *gid_out = sessions[i].gid;
            return 0;
        }
    }
    return -1;
}
