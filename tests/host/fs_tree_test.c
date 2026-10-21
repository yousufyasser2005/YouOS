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

#include "../../user/src/fs_tree.h"

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

int main(int argc, char** argv) {
    dfd = open(argv[1], O_RDWR); if (dfd < 0) { perror("img"); return 1; }
    if (getenv("FAKE_STEP")) fake_step = (uint64_t)atoi(getenv("FAKE_STEP"));
    if (ycfs_init() != 0) { puts("init failed"); return 1; }
    const char* cmd = argv[2]; int rc;
    if (!strcmp(cmd, "copy"))        rc = ft_copy_tree(argv[3], argv[4], atoi(argv[5]), 0);
    else if (!strcmp(cmd, "remove")) rc = ft_remove_tree(argv[3], atoi(argv[4]), 0);
    else if (!strcmp(cmd, "job"))    return run_job(argv[3], argv[4], atoi(argv[5]), (unsigned)atoi(argv[6]), argc > 7 ? atoi(argv[7]) : -1);
    else if (!strcmp(cmd, "jobdel")) return run_del_job(argv[3], atoi(argv[4]), (unsigned)atoi(argv[5]), argc > 6 ? atoi(argv[6]) : -1);
    else if (!strcmp(cmd, "move"))   { rc = sys_rename(argv[3], argv[4]); if (rc) ft_err = "rename refused"; }
    else { puts("bad command"); return 2; }
    printf("%s %s -> %s%s%s\n", cmd, argv[3], rc == 0 ? "OK" : "REFUSED: ", rc == 0 ? "" : ft_err, "");
    return rc == 0 ? 0 : 3;
}
