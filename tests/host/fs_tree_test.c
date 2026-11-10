/* Host test for user/src/fs_tree.h (the file manager's copy / delete).
 * fs_tree.h is compiled UNCHANGED; its sys_* calls are routed straight
 * into the real kernel/fs/ycfs.c running on a file-backed fake disk.
 *   fs_tree_test IMG copy SRC DST ISDIR | remove PATH ISDIR | move SRC DST */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <kernel/ycfs.h>

static int dfd;
int ata_read_sectors(uint64_t lba, uint8_t c, void* b)        { return pread(dfd, b, (size_t)c * 512, (off_t)(lba * 512)) == (ssize_t)c * 512 ? 0 : -1; }
int ata_write_sectors(uint64_t lba, uint8_t c, const void* b) { return pwrite(dfd, b, (size_t)c * 512, (off_t)(lba * 512)) == (ssize_t)c * 512 ? 0 : -1; }
int ata_write_sectors_nf(uint64_t lba, uint8_t c, const void* b) { return ata_write_sectors(lba, c, b); }
int ata_flush_now(void) { return 0; }
static long n_lock = 0, n_unlock = 0;
void storage_lock(void) { n_lock++; }
void storage_unlock(void) { n_unlock++; }

int session_lookup(uint32_t* u, uint32_t* g) { *u = 0; *g = 0; return 0; }
int session_is_elevated(void) { return 1; }
void vga_puts_color(const char* s, int a, int b) { (void)s; (void)a; (void)b; }

/* ---- the syscall wrappers fs_tree.h expects, backed by the kernel code ---- */
static struct { int used; uint32_t ino; uint64_t off, size; } fds[32];
/* resolve a path component by component, like the VFS layer does */
static int resolve_path(const char* p, uint32_t* ino, uint32_t* type, uint64_t* size_out) {
    uint32_t cur = ycfs_root_inode(), t = YCFS_TYPE_DIR; uint64_t sz = 0;
    if (!strncmp(p, "/ycfs", 5)) p += 5;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char name[56]; int k = 0;
        while (*p && *p != '/' && k < 55) name[k++] = *p++;
        name[k] = 0;
        if (ycfs_lookup(cur, name, &cur, &t, &sz) != 0) return -1;
    }
    *ino = cur; *type = t; *size_out = sz; return 0;
}
static int sys_open(const char* p, int f) {
    (void)f; uint32_t ino, type; uint64_t size;
    if (resolve_path(p, &ino, &type, &size) != 0) return -1;
    for (int i = 0; i < 32; i++) if (!fds[i].used) { fds[i].used = 1; fds[i].ino = ino; fds[i].off = 0; fds[i].size = size; return i; }
    return -1;
}
static int sys_close(int fd) { fds[fd].used = 0; return 0; }
static int64_t sys_fread(int fd, void* b, uint64_t l) {
    int64_t n = ycfs_read(fds[fd].ino, fds[fd].off, (uint32_t)l, b);
    if (n > 0) fds[fd].off += (uint64_t)n;
    return n;
}
/* Pacing clock for the job engine: every read advances it by fake_step ms, so a
 * run is deterministic. fake_step=1 simulates a fast disk, 200 a very slow one. */
static uint64_t fake_ms = 0, fake_step = 1;
#define FT_NOW() (fake_ms += fake_step)
static uint64_t sys_ticks(void) { return fake_ms / 10; }
static int64_t  sys_fsize(int fd) { return (int64_t)fds[fd].size; }
static int  sys_mkdir(const char* p)                    { return ycfs_mkdir(p); }
static int  sys_unlink(const char* p)                   { return ycfs_unlink(p); }
static int  sys_rename(const char* a, const char* b)    { return ycfs_rename(a, b); }
static long sys_readdir2(const char* p, void* b, long m){ return ycfs_list_dir(p, b, (int)m); }
static int64_t sys_save_chunk(const char* p, const void* b, uint64_t l, uint64_t o) { return ycfs_write_chunk(p, o, b, (uint32_t)l); }

/* ---- message queues, like kernel/ipc.c: 128-byte messages, 32 per queue, recv never blocks ---- */
#define MQ_N 4
static struct { char name[32]; unsigned char m[32][128]; unsigned len[32]; int head, count; } mq[MQ_N];
static int mq_find(const char* n, int create) {
    for (int i = 0; i < MQ_N; i++) if (mq[i].name[0] && !strcmp(mq[i].name, n)) return i;
    if (!create) return -1;
    for (int i = 0; i < MQ_N; i++) if (!mq[i].name[0]) { strncpy(mq[i].name, n, 31); return i; }
    return -1;
}
static int sys_mqcreate(const char* n) { return mq_find(n, 1) < 0 ? -1 : 0; }
static int sys_msgpost(const char* n, const void* d, unsigned len) {
    int q = mq_find(n, 1); if (q < 0 || mq[q].count >= 32) return -1;
    if (len > 128) len = 128;
    int t = (mq[q].head + mq[q].count) % 32; memcpy(mq[q].m[t], d, len); mq[q].len[t] = len; mq[q].count++; return 0;
}
static int sys_msgrecv(const char* n, void* d, unsigned* len, unsigned* from) {
    int q = mq_find(n, 0); if (q < 0 || mq[q].count == 0) return -1;
    memcpy(d, mq[q].m[mq[q].head], mq[q].len[mq[q].head]); if (len) *len = mq[q].len[mq[q].head]; if (from) *from = 77;
    mq[q].head = (mq[q].head + 1) % 32; mq[q].count--; return 0;
}
static void sys_sleep(unsigned long long ticks) { fake_ms += ticks * 10; }
static int  mq_pending(const char* n) { int q = mq_find(n, 0); return q < 0 ? 0 : mq[q].count; }

#include "../../user/src/fs_tree.h"
#include "../../user/src/xfer_proto.h"

/* Run a job to the end in small slices; optionally cancel after N steps. */
static int run_job(const char* src, const char* dst, int is_dir, unsigned budget, int cancel_after) {
    if (ft_job_start(src, dst, is_dir) != 0) { printf("job REFUSED at start: %s\n", ft_err); return 3; }
    int steps = 0, last_pct = 0, regress = 0; unsigned long long max_moved = 0;
    while (ftj_active()) {
        if (cancel_after >= 0 && steps == cancel_after) { ft_job_cancel(); break; }
        unsigned long long before = ft_job.done_bytes;
        ft_job_step(budget); steps++;
        unsigned long long moved = ft_job.done_bytes - before;
        if (ft_job.state == FTJ_COPY && moved > max_moved) max_moved = moved;
        int pct = ft_job_percent(); if (pct < last_pct) regress = 1; last_pct = pct;
        if (steps > 5000000) { printf("job did not finish\n"); return 4; }
    }
    printf("job %s: steps=%d max_bytes_per_step=%llu bytes=%llu/%llu files=%u/%u pct_regressed=%d chunk_len=%u%s%s\n",
        ft_job.state == FTJ_DONE ? "DONE" : ft_job.state == FTJ_CANCELLED ? "CANCELLED" : "FAILED",
        steps, max_moved, ft_job.done_bytes, ft_job.total_bytes, ft_job.done_files, ft_job.total_files, regress, ft_job.chunk_len,
        ft_job.state == FTJ_ERROR ? ": " : "", ft_job.state == FTJ_ERROR ? ft_job.err : "");
    return ft_job.state == FTJ_DONE ? 0 : 3;
}

/* Run a delete job to the end in small slices; optionally cancel after N steps. */
static int run_del_job(const char* path, int is_dir, unsigned budget, int cancel_after) {
    if (ft_job_start_delete(path, is_dir) != 0) { printf("deljob REFUSED at start: %s\n", ft_err); return 3; }
    int steps = 0, last_pct = 0, regress = 0;
    while (ftj_active()) {
        if (cancel_after >= 0 && steps == cancel_after) { ft_job_cancel(); break; }
        ft_job_step(budget); steps++;
        int pct = ft_job_percent(); if (pct < last_pct) regress = 1; last_pct = pct;
        if (steps > 5000000) { printf("deljob did not finish\n"); return 4; }
    }
    printf("deljob %s: steps=%d items=%u/%u known=%d pct_regressed=%d%s%s\n",
        ft_job.state == FTJ_DONE ? "DONE" : ft_job.state == FTJ_CANCELLED ? "CANCELLED" : "FAILED",
        steps, ft_job.done_items, ft_job.total_items, ft_job.total_known, regress,
        ft_job.state == FTJ_ERROR ? ": " : "", ft_job.state == FTJ_ERROR ? ft_job.err : "");
    return ft_job.state == FTJ_DONE ? 0 : 3;
}

/* Drive a transfer the way the real system does it: the desktop queues the job, the worker (xw_poll)
 * runs it, the desktop mirrors the snapshots it receives. `cancel_after` = send a cancel after that
 * many worker polls; `slow_desktop` = the desktop drains its queue only every 64th poll. */
static int run_wjob(int kind, const char* src, const char* dst, int is_dir, int cancel_after, int slow_desktop, int pre_clutter) {
    FtJob mirror; char* mp = (char*)&mirror; for (unsigned i = 0; i < sizeof mirror; i++) mp[i] = 0;
    char errstore[40]; errstore[0] = 0;
    if (pre_clutter) {                                   /* leftovers from an earlier job must not confuse this one */
        XferMsg junk; memset(&junk, 0x5A, sizeof junk); sys_msgpost(XFER_Q_UP, &junk, sizeof junk);
        XferCmd cj; memset(&cj, 0, sizeof cj); cj.type = 'X'; sys_msgpost(XFER_Q_DN, &cj, sizeof cj);
    }
    if (xr_send_job(kind, is_dir, src, dst) != 0) { printf("wjob SEND FAILED\n"); return 5; }
    mirror.state = FTJ_SCAN; mirror.kind = kind;
    XferWorker w; xw_init(&w);
    char* wj = (char*)&ft_job; for (unsigned i = 0; i < sizeof ft_job; i++) wj[i] = 0;   /* a fresh worker process starts with zeroed globals */
    int polls = 0, running = 1, msgs = 0, regress = 0, max_q = 0; unsigned long long prev_done = 0;
    while (running) {
        if (cancel_after >= 0 && polls == cancel_after) xr_cancel();
        running = xw_poll(&w); polls++;
        int q = mq_pending(XFER_Q_UP); if (q > max_q) max_q = q;
        if (!slow_desktop || !running || polls % 64 == 0) msgs += xr_pump(&mirror, errstore);
        if (kind == FTK_COPY && mirror.state == FTJ_COPY) { if (mirror.done_bytes < prev_done) regress = 1; prev_done = mirror.done_bytes; }
        if (polls > 3000000) { printf("wjob HUNG\n"); return 4; }
    }
    msgs += xr_pump(&mirror, errstore);
    printf("wjob %s: polls=%d msgs=%d regress=%d max_queue=%d bytes=%llu/%llu files=%u/%u items=%u/%u%s%s\n",
        mirror.state == FTJ_DONE ? "DONE" : mirror.state == FTJ_CANCELLED ? "CANCELLED" : mirror.state == FTJ_ERROR ? "FAILED" : "UNFINISHED",
        polls, msgs, regress, max_q, mirror.done_bytes, mirror.total_bytes, mirror.done_files, mirror.total_files, mirror.done_items, mirror.total_items,
        mirror.state == FTJ_ERROR ? ": " : "", mirror.state == FTJ_ERROR ? mirror.err : "");
    return mirror.state == FTJ_DONE ? 0 : mirror.state == FTJ_CANCELLED ? 2 : 3;
}
/* Protocol corner cases that need no filesystem */
static int run_wproto(void) {
    int bad = 0;
    /* long paths survive the split into pieces */
    char src[FT_PATH_MAX], dst[FT_PATH_MAX]; int k;
    for (k = 0; k < FT_PATH_MAX - 1; k++) { src[k] = (char)('a' + k % 26); dst[k] = (char)('A' + k % 26); } src[k] = dst[k] = 0;
    xr_send_job(FTK_COPY, 1, src, dst);
    XferWorker w; xw_init(&w); xw_read_cmds(&w);
    if (!w.go || !w.have_k || w.kind != FTK_COPY || !w.is_dir) { puts("wproto: job header lost"); bad++; }
    if (strcmp(w.src, src) || strcmp(w.dst, dst)) { puts("wproto: 219-char paths damaged by the split"); bad++; }
    /* a job that never says "go" is abandoned with an error, not waited for forever */
    xr_drain(XFER_Q_DN); xr_drain(XFER_Q_UP);
    XferWorker w2; xw_init(&w2); int n = 0; while (xw_poll(&w2) && n++ < 100000) { }
    FtJob mirror; char* mp = (char*)&mirror; for (unsigned i = 0; i < sizeof mirror; i++) mp[i] = 0; char es[40];
    xr_pump(&mirror, es);
    if (mirror.state != FTJ_ERROR || !mirror.err[0]) { puts("wproto: missing request not reported as an error"); bad++; }
    /* garbage on the progress queue is ignored */
    xr_drain(XFER_Q_UP); { unsigned char junk[128]; memset(junk, 0xEE, sizeof junk); sys_msgpost(XFER_Q_UP, junk, 128); sys_msgpost(XFER_Q_UP, junk, 40); }
    for (unsigned i = 0; i < sizeof mirror; i++) mp[i] = 0; mirror.state = FTJ_SCAN;
    if (xr_pump(&mirror, es) != 0 || mirror.state != FTJ_SCAN) { puts("wproto: garbage accepted as progress"); bad++; }
    /* a full queue must not stop the final report from arriving once the desktop drains it */
    xr_drain(XFER_Q_UP); { XferMsg f; memset(&f, 0, sizeof f); f.magic = XFER_MAGIC; for (int i = 0; i < 32; i++) sys_msgpost(XFER_Q_UP, &f, sizeof f); }
    ft_job.state = FTJ_DONE; ft_job.err = ""; XferWorker w3; xw_init(&w3); w3.phase = XW_FINAL;
    int r1 = xw_poll(&w3); if (r1 != 1 || w3.phase != XW_FINAL) { puts("wproto: worker gave up on a full queue"); bad++; }
    xr_drain(XFER_Q_UP); int r2 = xw_poll(&w3);
    if (r2 != 0 || mq_pending(XFER_Q_UP) != 1) { puts("wproto: final report not delivered after the queue drained"); bad++; }
    printf("wproto %s\n", bad ? "FAILED" : "OK");
    return bad ? 3 : 0;
}

int main(int argc, char** argv) {
    dfd = open(argv[1], O_RDWR); if (dfd < 0) { perror("img"); return 1; }
    if (getenv("FAKE_STEP")) fake_step = (uint64_t)atoi(getenv("FAKE_STEP"));
    if (ycfs_init() != 0) { puts("init failed"); return 1; }
    const char* cmd = argv[2]; int rc;
    if (!strcmp(cmd, "copy"))        rc = ft_copy_tree(argv[3], argv[4], atoi(argv[5]), 0);
    else if (!strcmp(cmd, "remove")) rc = ft_remove_tree(argv[3], atoi(argv[4]), 0);
    else if (!strcmp(cmd, "job"))    return run_job(argv[3], argv[4], atoi(argv[5]), (unsigned)atoi(argv[6]), argc > 7 ? atoi(argv[7]) : -1);
    else if (!strcmp(cmd, "jobdel")) return run_del_job(argv[3], atoi(argv[4]), (unsigned)atoi(argv[5]), argc > 6 ? atoi(argv[6]) : -1);
    else if (!strcmp(cmd, "wjob"))   return run_wjob(atoi(argv[3]), argv[4], argv[5], atoi(argv[6]), argc > 7 ? atoi(argv[7]) : -1, argc > 8 ? atoi(argv[8]) : 0, argc > 9 ? atoi(argv[9]) : 0);
    else if (!strcmp(cmd, "wproto")) return run_wproto();
    else if (!strcmp(cmd, "move"))   { rc = sys_rename(argv[3], argv[4]); if (rc) ft_err = "rename refused"; }
    else { puts("bad command"); return 2; }
    printf("%s %s -> %s%s%s\n", cmd, argv[3], rc == 0 ? "OK" : "REFUSED: ", rc == 0 ? "" : ft_err, "");
    return rc == 0 ? 0 : 3;
}
