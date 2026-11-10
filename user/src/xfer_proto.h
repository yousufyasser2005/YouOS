/* xfer_proto.h -- background file transfers: the desktop <-> `copyd` worker protocol.
 *
 * The desktop no longer copies or deletes files itself. It starts the `copyd` program, tells it
 * what to do over a message queue, and shows the progress it reports back. Because `copyd` is a
 * separate process, its waits for the disk (which sleep, see kernel/proc/sync.c) leave the CPU to
 * the desktop: the transfer neither freezes the cursor nor has to be throttled while you move it.
 *
 *   desktop -> worker  queue "xfer_dn"   XferCmd  (128 bytes)
 *       'K' a=kind (FTK_COPY/FTK_DELETE) b=is_dir      what to do
 *       'S' a=part  len  data                          source path, in pieces of <=120 bytes
 *       'D' a=part  len  data                          destination path, same
 *       'G'                                            go (all pieces sent)
 *       'X'                                            cancel: stop, remove what was written
 *   worker -> desktop  queue "xfer_up"   XferMsg  (128 bytes)   a snapshot of the job state
 *
 * Header-only and free of desktop dependencies: the worker logic (XferWorker) is compiled unchanged
 * into the host test (tests/host/xfer_test.c) against the real filesystem code.
 * Needs the sys_* wrappers and fs_tree.h (FtJob, ft_job_*) to be visible first. */
#ifndef XFER_PROTO_H
#define XFER_PROTO_H

#define XFER_Q_UP   "xfer_up"
#define XFER_Q_DN   "xfer_dn"
#define XFER_MAGIC  0x58465231u
#define XFER_PIECE  120

typedef struct {
    unsigned           magic;
    unsigned char      state, kind, total_known, pad;
    unsigned long long total_bytes, done_bytes;
    unsigned           total_files, done_files, total_items, done_items;
    unsigned           chunk_len, last_chunk_ms, rate_bpms;
    char               cur_name[40];
    char               err[36];
} XferMsg;
typedef char xfer_msg_must_be_128_bytes[(sizeof(XferMsg) == 128) ? 1 : -1];

typedef struct { unsigned char type, a, b, len; char data[124]; } XferCmd;
typedef char xfer_cmd_must_be_128_bytes[(sizeof(XferCmd) == 128) ? 1 : -1];

static void xfer_strcpy_n(char* d, const char* s, int max) {      /* copy at most max-1 chars, always terminate */
    int i = 0; if (s) while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static int xfer_strlen(const char* s) { int n = 0; while (s[n]) n++; return n; }

/* ---- snapshot <-> job state ---- */
static void xfer_pack(const FtJob* j, XferMsg* m) {
    char* p = (char*)m; for (unsigned i = 0; i < sizeof *m; i++) p[i] = 0;
    m->magic = XFER_MAGIC; m->state = (unsigned char)j->state; m->kind = (unsigned char)j->kind;
    m->total_known = (unsigned char)(j->total_known ? 1 : 0);
    m->total_bytes = j->total_bytes; m->done_bytes = j->done_bytes;
    m->total_files = j->total_files; m->done_files = j->done_files;
    m->total_items = j->total_items; m->done_items = j->done_items;
    m->chunk_len = j->chunk_len;
    m->last_chunk_ms = j->last_chunk_ms > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned)j->last_chunk_ms;
    m->rate_bpms = j->rate_bpms > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned)j->rate_bpms;
    xfer_strcpy_n(m->cur_name, j->cur_name, (int)sizeof m->cur_name);
    xfer_strcpy_n(m->err, j->err, (int)sizeof m->err);
}
/* Apply a snapshot to the desktop's mirror of the job. t_start and target_ms are the desktop's own. */
static void xfer_apply(FtJob* j, const XferMsg* m, char* err_store /* >= 36 bytes */) {
    j->state = m->state; j->kind = m->kind; j->total_known = m->total_known;
    j->total_bytes = m->total_bytes; j->done_bytes = m->done_bytes;
    j->total_files = m->total_files; j->done_files = m->done_files;
    j->total_items = m->total_items; j->done_items = m->done_items;
    j->chunk_len = m->chunk_len; j->last_chunk_ms = m->last_chunk_ms; j->rate_bpms = m->rate_bpms;
    xfer_strcpy_n(j->cur_name, m->cur_name, (int)sizeof j->cur_name);
    xfer_strcpy_n(err_store, m->err, (int)sizeof m->err);
    j->err = err_store;
}

/* ---- desktop side ---- */
static void xr_drain(const char* q) { unsigned char b[128]; unsigned l, f; while (sys_msgrecv(q, b, &l, &f) == 0) { } }

static int xr_post_cmd(unsigned char type, unsigned char a, unsigned char b, const char* data, int len) {
    XferCmd c; char* p = (char*)&c; for (unsigned i = 0; i < sizeof c; i++) p[i] = 0;
    c.type = type; c.a = a; c.b = b; c.len = (unsigned char)len;
    for (int i = 0; i < len; i++) c.data[i] = data[i];
    return sys_msgpost(XFER_Q_DN, &c, (unsigned)sizeof c);
}
static int xr_post_path(unsigned char type, const char* path) {
    int n = xfer_strlen(path), part = 0;
    if (n == 0) return xr_post_cmd(type, 0, 0, path, 0);
    for (int off = 0; off < n; off += XFER_PIECE, part++) {
        int l = n - off > XFER_PIECE ? XFER_PIECE : n - off;
        if (xr_post_cmd(type, (unsigned char)part, 0, path + off, l) != 0) return -1;
    }
    return 0;
}
/* Queue the whole job for the worker. Clears anything left over from an earlier job first.
 * 0 = queued, -1 = a queue was full or missing. */
static int xr_send_job(int kind, int is_dir, const char* src, const char* dst) {
    sys_mqcreate(XFER_Q_UP); sys_mqcreate(XFER_Q_DN);
    xr_drain(XFER_Q_UP); xr_drain(XFER_Q_DN);
    if (xr_post_cmd('K', (unsigned char)kind, (unsigned char)(is_dir ? 1 : 0), 0, 0) != 0) return -1;
    if (xr_post_path('S', src) != 0) return -1;
    if (xr_post_path('D', dst ? dst : "") != 0) return -1;
    return xr_post_cmd('G', 0, 0, 0, 0);
}
static int xr_cancel(void) { return xr_post_cmd('X', 0, 0, 0, 0); }
/* Apply every pending snapshot to the mirror; returns how many there were. Anything that is not a
 * well-formed snapshot is ignored. */
static int xr_pump(FtJob* mirror, char* err_store) {
    XferMsg m; unsigned l = 0, f = 0; int n = 0;
    while (sys_msgrecv(XFER_Q_UP, &m, &l, &f) == 0) {
        if (l == sizeof m && m.magic == XFER_MAGIC) { xfer_apply(mirror, &m, err_store); n++; }
    }
    return n;
}

/* ---- worker side ----
 * One xw_poll() does a bounded amount of work and returns 1 while there is more to do, 0 when the
 * final snapshot has been delivered and the process may exit. */
enum { XW_WAIT_JOB = 0, XW_RUN = 1, XW_FINAL = 2, XW_DONE = 3 };
#define XW_JOB_TIMEOUT_MS 8000
#define XW_PROGRESS_MS    100

typedef struct {
    int phase;
    int kind, is_dir, have_k, have_s, have_d, go, cancel;
    char src[FT_PATH_MAX], dst[FT_PATH_MAX];
    unsigned long long t0, last_post;
} XferWorker;

static void xw_init(XferWorker* w) {
    char* p = (char*)w; for (unsigned i = 0; i < sizeof *w; i++) p[i] = 0;
    w->phase = XW_WAIT_JOB; w->t0 = FT_NOW();
}
static void xw_post_state(int final_) {
    XferMsg m; xfer_pack(&ft_job, &m);
    int rc = sys_msgpost(XFER_Q_UP, &m, (unsigned)sizeof m);
    (void)final_; (void)rc;
}
/* Read pending commands. Returns without blocking. */
static void xw_read_cmds(XferWorker* w) {
    XferCmd c; unsigned l, f;
    while (sys_msgrecv(XFER_Q_DN, &c, &l, &f) == 0) {
        if (l != sizeof c) continue;
        if (c.type == 'K') { w->kind = c.a; w->is_dir = c.b; w->have_k = 1; w->src[0] = w->dst[0] = 0; }
        else if (c.type == 'S' || c.type == 'D') {
            char* d = c.type == 'S' ? w->src : w->dst; int off = (int)c.a * XFER_PIECE;
            if (off + c.len < FT_PATH_MAX && c.len <= XFER_PIECE) {
                for (int i = 0; i < c.len; i++) d[off + i] = c.data[i];
                d[off + c.len] = 0;
                if (c.type == 'S') w->have_s = 1; else w->have_d = 1;
            }
        }
        else if (c.type == 'G') w->go = 1;
        else if (c.type == 'X') w->cancel = 1;
    }
}
static void xw_fail(const char* msg) {
    ft_job.state = FTJ_ERROR; ft_job.err = msg; ft_err = msg;
}
static int xw_poll(XferWorker* w) {
    switch (w->phase) {
    case XW_WAIT_JOB:
        xw_read_cmds(w);
        if (w->cancel) { ft_job.state = FTJ_CANCELLED; ft_job.err = ""; w->phase = XW_FINAL; return 1; }
        if (w->go) {
            if (!w->have_k || !w->have_s) { xw_fail("transfer request incomplete"); ft_job.kind = w->kind; w->phase = XW_FINAL; return 1; }
            int r = w->kind == FTK_DELETE ? ft_job_start_delete(w->src, w->is_dir) : ft_job_start(w->src, w->dst, w->is_dir);
            if (r != 0) { const char* e = ft_err; ft_job.state = FTJ_ERROR; ft_job.kind = w->kind; ft_job.err = e; w->phase = XW_FINAL; return 1; }
            ft_job.target_ms = 1000;                    /* nobody to keep smooth here: let the chunk grow to its maximum */
            w->phase = XW_RUN; w->last_post = 0;
            return 1;
        }
        if (FT_NOW() - w->t0 > XW_JOB_TIMEOUT_MS) { xw_fail("no transfer request received"); w->phase = XW_FINAL; return 1; }
        sys_sleep(1);
        return 1;
    case XW_RUN: {
        xw_read_cmds(w);
        if (w->cancel) { w->cancel = 0; ft_job_cancel(); }            /* removes whatever was written */
        if (ftj_active()) ft_job_step_n(100, 8);
        unsigned long long now = FT_NOW();
        if (!ftj_active()) { w->phase = XW_FINAL; return 1; }
        if (now - w->last_post >= XW_PROGRESS_MS || w->last_post == 0) { xw_post_state(0); w->last_post = now; }
        return 1;
    }
    case XW_FINAL: {
        XferMsg m; xfer_pack(&ft_job, &m);
        if (sys_msgpost(XFER_Q_UP, &m, (unsigned)sizeof m) == 0) { w->phase = XW_DONE; return 0; }
        sys_sleep(1);                                   /* queue full: the desktop has not drained it yet; try again */
        return 1;
    }
    default: return 0;
    }
}

#endif /* XFER_PROTO_H */
