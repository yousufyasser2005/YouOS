/* fs_tree.h -- whole-tree file operations for the file manager:
 * copy (files of any size, folders recursively) and recursive delete.
 *
 * Header-only and free of desktop dependencies so it compiles unchanged in
 * a host test harness (tests/host/) against the real kernel filesystem.
 * Needs the sys_* wrappers from ../lib/syscall.h to be visible first.
 *
 * Moving (Cut + Paste) does NOT go through here: that is one sys_rename(),
 * which is instant, has no size limit, and keeps a folder's contents
 * attached to it.
 *
 * Every function returns 0 on success; on failure it returns -1 and sets
 * ft_err to a short message (<= 47 chars, fits the file manager's status
 * line).
 */
#ifndef FS_TREE_H
#define FS_TREE_H

#ifndef FT_PATH_MAX
#define FT_PATH_MAX    220
#endif
#define FT_MAX_DEPTH   6        /* folder nesting we will recurse into */
#define FT_MAX_ENTRIES 256      /* entries per folder (the kernel call is capped by us) */
#define FT_CHUNK       1048576  /* largest read/write round trip (1 MB) */
#define FT_CHUNK_MIN   16384    /* smallest a round trip will shrink to */
#define FT_CHUNK_START 16384    /* first round trip */
#define FT_TARGET_MS   250      /* default hitch limit: longest one round trip may take (the desktop lowers/raises it) */

#ifndef FT_DIRENT_DEFINED
#define FT_DIRENT_DEFINED
/* Layout of one entry returned by sys_readdir2 / sys_readdir. */
typedef struct { char name[32]; unsigned int size; unsigned char is_dir; } Dirent;
#endif

/* Clock used to pace a copy job, in milliseconds. The default counts the
 * 100 Hz tick, but the kernel runs every syscall with interrupts OFF, so a
 * long disk write makes the tick fall behind and a tick-based budget badly
 * underestimates how long a slice took. The desktop therefore defines
 * FT_NOW() before including this file, using the CPU's cycle counter. */
#ifndef FT_NOW
#define FT_NOW() ((unsigned long long)sys_ticks() * 10ULL)
#endif

static const char* ft_err = "";
static unsigned char ft_chunk[FT_CHUNK];
static Dirent        ft_ents[FT_MAX_DEPTH][FT_MAX_ENTRIES];

/* Delete works in small batches instead (list a few entries, remove them,
 * list again), so it has no entry-count limit and a generous depth limit. */
#define FT_RM_DEPTH 24
#define FT_RM_BATCH 32
static Dirent        ft_rm_ents[FT_RM_DEPTH][FT_RM_BATCH];

static int ft_len(const char* s) { int n = 0; while (s[n]) n++; return n; }

/* out = dir + "/" + name (no doubled slash). Returns 0, or -1 if it doesn't fit. */
static int ft_join(char* out, int outsz, const char* dir, const char* name) {
    int k = 0, jd = 0, jn = 0;
    while (dir[jd] && k < outsz - 1) out[k++] = dir[jd++];
    if (k > 0 && out[k - 1] != '/' && k < outsz - 1) out[k++] = '/';
    while (name[jn] && k < outsz - 1) out[k++] = name[jn++];
    out[k] = 0;
    return (dir[jd] || name[jn]) ? -1 : 0;     /* anything left over = truncated */
}

static int ft_exists(const char* path) {
    int fd = sys_open(path, 0);
    if (fd < 0) return 0;
    sys_close(fd);
    return 1;
}

/* 1 if `path` is `dir` itself or lies inside it (so copying dir into path
 * would recurse forever). */
static int ft_is_inside(const char* path, const char* dir) {
    int i = 0;
    while (dir[i] && path[i] == dir[i]) i++;
    if (dir[i] != 0) return 0;
    return path[i] == 0 || path[i] == '/';
}

static int ft_fail(const char* msg) { ft_err = msg; return -1; }

/* List a folder into ft_ents[depth]. Returns the entry count, or -1. */
static int ft_list(const char* path, int depth) {
    long n = sys_readdir2(path, ft_ents[depth], FT_MAX_ENTRIES);
    if (n < 0) return ft_fail("cannot read folder");
    if (n >= FT_MAX_ENTRIES) return ft_fail("folder has too many items (max 255)");
    return (int)n;
}

/* Copy one file in FT_CHUNK pieces. Overwrites nothing silently: the caller
 * has already picked a free destination name. */
static int ft_copy_file(const char* src, const char* dst) {
    int fd = sys_open(src, 0);
    if (fd < 0) return ft_fail("cannot open source file");
    unsigned long long off = 0;
    for (;;) {
        long long n = sys_fread(fd, ft_chunk, FT_CHUNK);
        if (n < 0) { sys_close(fd); sys_unlink(dst); return ft_fail("read error while copying"); }
        if (n == 0) break;
        long long w = sys_save_chunk(dst, ft_chunk, (unsigned long long)n, off);
        if (w != n) { sys_close(fd); sys_unlink(dst); return ft_fail("write failed (disk full or no access)"); }
        off += (unsigned long long)n;
    }
    sys_close(fd);
    if (off == 0 && sys_save_chunk(dst, ft_chunk, 0, 0) != 0)    /* empty file still has to exist */
        return ft_fail("cannot create file");
    return 0;
}

static int ft_remove_tree(const char* path, int is_dir, int depth);

/* Copy `src` (file or folder) to the not-yet-existing `dst`. */
static int ft_copy_tree(const char* src, const char* dst, int is_dir, int depth) {
    if (!is_dir) return ft_copy_file(src, dst);
    if (depth >= FT_MAX_DEPTH) return ft_fail("folders nested too deeply (max 6)");
    if (ft_is_inside(dst, src)) return ft_fail("cannot copy a folder into itself");
    if (sys_mkdir(dst) != 0) return ft_fail("cannot create folder");
    int n = ft_list(src, depth);
    if (n < 0) { sys_unlink(dst); return -1; }
    for (int i = 0; i < n; i++) {
        char s2[FT_PATH_MAX], d2[FT_PATH_MAX];
        if (ft_join(s2, sizeof s2, src, ft_ents[depth][i].name) != 0 ||
            ft_join(d2, sizeof d2, dst, ft_ents[depth][i].name) != 0) {
            ft_remove_tree(dst, 1, depth);
            return ft_fail("path too long");
        }
        if (ft_copy_tree(s2, d2, ft_ents[depth][i].is_dir, depth + 1) != 0) {
            if (depth == 0) ft_remove_tree(dst, 1, 0);      /* don't leave a half copy behind */
            return -1;
        }
    }
    return 0;
}

/* Delete a file, or a folder and everything inside it. (The kernel refuses
 * to unlink a non-empty folder, so contents must go first.) Folders of any
 * size work: each pass lists up to FT_RM_BATCH entries, removes them, and
 * lists again until the folder is empty. */
static int ft_remove_tree(const char* path, int is_dir, int depth) {
    if (is_dir) {
        if (depth >= FT_RM_DEPTH) return ft_fail("folders nested too deeply (max 24)");
        char first_prev[32]; first_prev[0] = 0;
        for (;;) {
            long n = sys_readdir2(path, ft_rm_ents[depth], FT_RM_BATCH);
            if (n < 0) return ft_fail("cannot read folder");
            if (n == 0) break;
            /* Safety net: if the first entry is the same one we just "removed",
             * nothing is actually going away -- stop instead of looping forever
             * (this runs inside the GUI process; a hang would freeze the desktop). */
            { int same = first_prev[0] != 0; for (int q = 0; q < 32 && same; q++) { if (ft_rm_ents[depth][0].name[q] != first_prev[q]) same = 0; if (!first_prev[q]) break; }
              if (same) return ft_fail("delete made no progress"); }
            for (int q = 0; q < 32; q++) first_prev[q] = ft_rm_ents[depth][0].name[q];
            for (long i = 0; i < n; i++) {
                char c[FT_PATH_MAX];
                if (ft_join(c, sizeof c, path, ft_rm_ents[depth][i].name) != 0) return ft_fail("path too long");
                if (ft_remove_tree(c, ft_rm_ents[depth][i].is_dir, depth + 1) != 0) return -1;
            }
        }
    }
    if (sys_unlink(path) != 0) return ft_fail("cannot delete (no access?)");
    return 0;
}


/* ═══ Incremental copy job (non-blocking) ═══════════════════════════════
 * ft_copy_tree() above does a whole copy in one call, which freezes the
 * desktop for as long as the copy takes. The job below does the same work
 * in small steps instead: the caller (the desktop's main loop) calls
 * ft_job_step() once per frame with a small time budget, and between
 * calls it is free to redraw, handle the mouse, show progress, or cancel.
 *
 * A job runs in two passes over the source tree:
 *   SCAN  walks it once to count files and bytes (so progress is a real
 *         percentage) and to find every limit problem (too many items in a
 *         folder, nested too deeply) BEFORE anything has been created --
 *         a refused copy leaves nothing behind;
 *   COPY  walks it again, creating folders and copying files one chunk at a
 *         time (one chunk or one directory entry per loop iteration).
 * A failed or cancelled job removes whatever it had already written.
 *
 * Uses sys_ticks() (10 ms units), sys_fsize() and sys_open/sys_fread/...
 */
#define FTJ_DEPTH 24       /* frames: copy is limited to FT_MAX_DEPTH, delete may nest this deep */

enum { FTJ_IDLE = 0, FTJ_SCAN, FTJ_COPY, FTJ_DONE, FTJ_ERROR, FTJ_CANCELLED };

typedef struct { char sp[FT_PATH_MAX]; char dp[FT_PATH_MAX]; int n, next; char first[32]; } FtFrame;

enum { FTK_COPY = 0, FTK_DELETE = 1 };

typedef struct {
    int  state;
    int  kind;                       /* FTK_COPY or FTK_DELETE */
    int  root_is_dir;
    char root_src[FT_PATH_MAX], root_dst[FT_PATH_MAX];
    FtFrame fr[FTJ_DEPTH];
    int  depth;                      /* frame being walked; -1 = none left */
    unsigned long long total_bytes, done_bytes;
    unsigned           total_files, done_files;
    int  fd;                         /* file being copied, or -1 */
    unsigned long long off;
    char fdst[FT_PATH_MAX];
    char cur_name[40];               /* for the progress dialog */
    unsigned long long t_start;      /* FT_NOW() when the job began */
    unsigned chunk_len;              /* current round-trip size, adapted to the disk's speed */
    unsigned long long rate_bpms;    /* smoothed throughput, bytes per millisecond */
    unsigned long long last_chunk_ms;/* how long the last round trip took (diagnostics) */
    unsigned long long max_step_bytes;   /* most data moved by one step (for tests/diagnostics) */
    unsigned long long target_ms;    /* hitch limit: the longest one copy round trip may take before the chunk is halved */
    unsigned total_items, done_items;/* delete jobs: files + folders */
    int  total_known;                /* 0 when a folder was too big to count (progress becomes indeterminate) */
    const char* err;
    int  dst_created;                /* something exists at root_dst that cleanup should remove */
} FtJob;

static FtJob ft_job;

static int ftj_active(void) { return ft_job.state == FTJ_SCAN || ft_job.state == FTJ_COPY; }

static void ftj_set_name(const char* n) {
    int k = 0; while (n[k] && k < 39) { ft_job.cur_name[k] = n[k]; k++; } ft_job.cur_name[k] = 0;
}
static void ftj_copy_str(char* d, const char* s, int max) {
    int k = 0; while (s[k] && k < max - 1) { d[k] = s[k]; k++; } d[k] = 0;
}

/* Stop with an error: close what is open and remove the partial copy. */
static int ftj_fail(const char* msg) {
    if (ft_job.fd >= 0) { sys_close(ft_job.fd); ft_job.fd = -1; }
    if (ft_job.dst_created) {
        ft_remove_tree(ft_job.root_dst, ft_job.root_is_dir, 0);
        ft_job.dst_created = 0;
    }
    ft_job.err = msg;
    ft_job.state = FTJ_ERROR;
    return -1;
}

static int ftj_list(int depth, const char* path) {
    long n = sys_readdir2(path, ft_ents[depth], FT_MAX_ENTRIES);
    if (n < 0) return ftj_fail("cannot read folder");
    if (n >= FT_MAX_ENTRIES) return ftj_fail("folder has too many items (max 255)");
    ft_job.fr[depth].n = (int)n;
    ft_job.fr[depth].next = 0;
    return 0;
}

/* Enter folder (sp -> dp). In the COPY pass the destination folder is made first. */
static int ftj_push(const char* sp, const char* dp, int make_dir) {
    if (ft_job.depth + 1 >= FT_MAX_DEPTH) return ftj_fail("folders nested too deeply (max 6)");
    if (make_dir) {
        if (sys_mkdir(dp) != 0) return ftj_fail("cannot create folder");
        if (ft_job.depth < 0) ft_job.dst_created = 1;      /* root folder now exists */
    }
    ft_job.depth++;
    ftj_copy_str(ft_job.fr[ft_job.depth].sp, sp, FT_PATH_MAX);
    ftj_copy_str(ft_job.fr[ft_job.depth].dp, dp, FT_PATH_MAX);
    return ftj_list(ft_job.depth, sp);
}

/* Begin copying `src` to the not-yet-existing `dst`. Returns 0 if the job
 * started (call ft_job_step() until it stops being active), else -1 with
 * ft_err set. */
static int ft_job_start(const char* src, const char* dst, int is_dir) {
    if (ftj_active()) { ft_err = "a transfer is already running"; return -1; }
    FtJob* j = &ft_job;
    j->state = FTJ_SCAN; j->kind = FTK_COPY; j->root_is_dir = is_dir; j->err = ""; j->fd = -1; j->off = 0;
    j->target_ms = FT_TARGET_MS; j->total_items = j->done_items = 0; j->total_known = 1;
    j->total_bytes = j->done_bytes = 0; j->total_files = j->done_files = 0;
    j->max_step_bytes = 0; j->dst_created = 0; j->depth = -1; j->cur_name[0] = 0;
    j->t_start = FT_NOW();
    j->chunk_len = FT_CHUNK_START; j->rate_bpms = 0; j->last_chunk_ms = 0;
    ftj_copy_str(j->root_src, src, FT_PATH_MAX);
    ftj_copy_str(j->root_dst, dst, FT_PATH_MAX);
    if (is_dir && ft_is_inside(dst, src)) { j->state = FTJ_ERROR; j->err = "cannot copy a folder into itself"; ft_err = j->err; return -1; }
    if (is_dir) {
        if (ftj_push(src, dst, 0) != 0) { ft_err = j->err; return -1; }
    } else {
        int fd = sys_open(src, 0);
        if (fd < 0) { j->state = FTJ_ERROR; j->err = "cannot open source file"; ft_err = j->err; return -1; }
        long long sz = sys_fsize(fd);
        sys_close(fd);
        j->total_files = 1; j->total_bytes = sz > 0 ? (unsigned long long)sz : 0;
    }
    return 0;
}

/* Abort a running job and remove whatever was written. */
static void ft_job_cancel(void) {
    if (!ftj_active()) return;
    if (ft_job.fd >= 0) { sys_close(ft_job.fd); ft_job.fd = -1; }
    if (ft_job.dst_created) {
        ft_remove_tree(ft_job.root_dst, ft_job.root_is_dir, 0);
        ft_job.dst_created = 0;
    }
    ft_job.state = FTJ_CANCELLED;
}

/* Reset a finished job so a new one can start. */
static void ft_job_clear(void) { if (!ftj_active()) ft_job.state = FTJ_IDLE; }

/* Switch from the SCAN pass to the COPY pass. */
static int ftj_begin_copy(void) {
    FtJob* j = &ft_job;
    j->state = FTJ_COPY; j->depth = -1; j->done_bytes = 0; j->done_files = 0;
    if (j->root_is_dir) return ftj_push(j->root_src, j->root_dst, 1);
    /* single file: set it up as the file in progress */
    j->fd = sys_open(j->root_src, 0);
    if (j->fd < 0) return ftj_fail("cannot open source file");
    ftj_copy_str(j->fdst, j->root_dst, FT_PATH_MAX);
    j->off = 0; j->dst_created = 1;
    { const char* b = j->root_src; const char* q = b; while (*q) { if (*q == '/') b = q + 1; q++; } ftj_set_name(b); }
    if (j->total_bytes == 0) {                      /* empty file: just create it */
        sys_close(j->fd); j->fd = -1;
        if (sys_save_chunk(j->fdst, ft_chunk, 0, 0) != 0) return ftj_fail("cannot create file");
        j->done_files = 1;
    }
    return 0;
}

/* ---- delete ------------------------------------------------------- */

static void ftj_basename_to_name(const char* path) {
    const char* b = path; const char* q = path;
    while (*q) { if (*q == '/') b = q + 1; q++; }
    ftj_set_name(b);
}

/* SCAN pass of a delete: count files and folders for a real percentage.
 * If any folder is too big or too deep for our listing buffers we simply
 * give up counting (progress becomes indeterminate) -- the delete pass
 * itself has no such limits. */
static int ftj_try_push_scan(const char* path) {
    if (ft_job.depth + 1 >= FT_MAX_DEPTH) return -1;
    long n = sys_readdir2(path, ft_ents[ft_job.depth + 1], FT_MAX_ENTRIES);
    if (n < 0 || n >= FT_MAX_ENTRIES) return -1;
    ft_job.depth++;
    ftj_copy_str(ft_job.fr[ft_job.depth].sp, path, FT_PATH_MAX);
    ft_job.fr[ft_job.depth].n = (int)n; ft_job.fr[ft_job.depth].next = 0;
    return 0;
}

static void ftj_del_begin(void) {
    FtJob* j = &ft_job;
    j->state = FTJ_COPY; j->depth = -1; j->done_items = 0;
    if (!j->root_is_dir) {
        ftj_basename_to_name(j->root_src);
        if (sys_unlink(j->root_src) != 0) { ftj_fail("cannot delete (no access?)"); return; }
        j->done_items = 1; j->state = FTJ_DONE;
        return;
    }
    j->depth = 0;
    ftj_copy_str(j->fr[0].sp, j->root_src, FT_PATH_MAX);
    j->fr[0].n = 0; j->fr[0].next = 0; j->fr[0].first[0] = 0;        /* listed on the first step */
}

static void ftj_del_scan_step(void) {
    FtJob* j = &ft_job;
    if (j->depth < 0) { ftj_del_begin(); return; }                   /* scan finished */
    FtFrame* f = &j->fr[j->depth];
    if (f->next >= f->n) { j->depth--; return; }
    Dirent* e = &ft_ents[j->depth][f->next++];
    j->total_items++;
    if (e->is_dir) {
        char c[FT_PATH_MAX];
        if (ft_join(c, sizeof c, f->sp, e->name) != 0 || ftj_try_push_scan(c) != 0) {
            j->total_known = 0;                                      /* too big to count: stop counting */
            j->depth = -1;
        }
    }
}

/* One delete operation per call: list a batch, then remove one entry (a file,
 * or step into a sub-folder), and finally the emptied folder itself. */
static void ftj_del_step(void) {
    FtJob* j = &ft_job;
    if (j->depth < 0) { j->state = FTJ_DONE; return; }
    FtFrame* f = &j->fr[j->depth];
    if (f->next >= f->n) {
        long n = sys_readdir2(f->sp, ft_rm_ents[j->depth], FT_RM_BATCH);
        if (n < 0) { ftj_fail("cannot read folder"); return; }
        if (n == 0) {                                                /* empty now: remove the folder itself */
            ftj_basename_to_name(f->sp);
            if (sys_unlink(f->sp) != 0) { ftj_fail("cannot delete folder (no access?)"); return; }
            j->done_items++; j->depth--;
            return;
        }
        /* Safety net: the same first entry coming back means nothing is
         * actually being removed -- stop rather than loop forever (this runs
         * inside the GUI process, a hang would freeze the desktop). */
        { int same = f->first[0] != 0;
          for (int q = 0; q < 32 && same; q++) { if (ft_rm_ents[j->depth][0].name[q] != f->first[q]) same = 0; if (!f->first[q]) break; }
          if (same) { ftj_fail("delete made no progress"); return; }
          for (int q = 0; q < 32; q++) f->first[q] = ft_rm_ents[j->depth][0].name[q]; }
        f->n = (int)n; f->next = 0;
        return;
    }
    Dirent* e = &ft_rm_ents[j->depth][f->next++];
    char c[FT_PATH_MAX];
    if (ft_join(c, sizeof c, f->sp, e->name) != 0) { ftj_fail("path too long"); return; }
    ftj_set_name(e->name);
    if (e->is_dir) {
        if (j->depth + 1 >= FTJ_DEPTH) { ftj_fail("folders nested too deeply (max 24)"); return; }
        j->depth++;
        ftj_copy_str(j->fr[j->depth].sp, c, FT_PATH_MAX);
        j->fr[j->depth].n = 0; j->fr[j->depth].next = 0; j->fr[j->depth].first[0] = 0;
    } else {
        if (sys_unlink(c) != 0) { ftj_fail("cannot delete (no access?)"); return; }
        j->done_items++;
    }
}

/* Begin deleting `path` (a file, or a folder and everything in it). Same
 * contract as ft_job_start(). */
static int ft_job_start_delete(const char* path, int is_dir) {
    if (ftj_active()) { ft_err = "a transfer is already running"; return -1; }
    FtJob* j = &ft_job;
    j->state = FTJ_SCAN; j->kind = FTK_DELETE; j->root_is_dir = is_dir; j->err = ""; j->fd = -1; j->off = 0;
    j->total_bytes = j->done_bytes = 0; j->total_files = j->done_files = 0;
    j->total_items = 1; j->done_items = 0; j->total_known = 1;      /* the root itself counts */
    j->max_step_bytes = 0; j->dst_created = 0; j->depth = -1; j->cur_name[0] = 0;
    j->target_ms = FT_TARGET_MS; j->chunk_len = FT_CHUNK_START; j->rate_bpms = 0; j->last_chunk_ms = 0;
    j->t_start = FT_NOW();
    ftj_copy_str(j->root_src, path, FT_PATH_MAX); j->root_dst[0] = 0;
    ftj_basename_to_name(path);
    if (is_dir && ftj_try_push_scan(path) != 0) { j->total_known = 0; j->depth = -1; }
    return 0;
}

/* Do up to `budget` milliseconds of work (and at most `max_iters` loop
 * iterations -- a safety cap that holds even if the clock is wrong), then
 * return the job state.
 * One loop iteration is one chunk of one file or one directory entry, so a
 * single call overshoots the budget by at most one such unit. */
static int ft_job_step_n(unsigned long long budget, unsigned max_iters) {
    FtJob* j = &ft_job;
    unsigned long long t0 = FT_NOW(), moved = 0;
    unsigned iters = 0;
    while (ftj_active()) {
        if (j->kind == FTK_DELETE) {
            if (j->state == FTJ_SCAN) ftj_del_scan_step(); else ftj_del_step();
        } else if (j->state == FTJ_COPY && j->fd >= 0) {
            unsigned long long c0 = FT_NOW();
            unsigned want_len = j->chunk_len;
            long long n = sys_fread(j->fd, ft_chunk, want_len);
            if (n < 0) { ftj_fail("read error while copying"); break; }
            if (n == 0) { sys_close(j->fd); j->fd = -1; j->done_files++; }
            else {
                long long w = sys_save_chunk(j->fdst, ft_chunk, (unsigned long long)n, j->off);
                if (w != n) { ftj_fail("write failed (disk full or no access)"); break; }
                j->off += (unsigned long long)n; j->done_bytes += (unsigned long long)n; moved += (unsigned long long)n;
                /* Pace: the cost of a round trip is mostly FIXED (a handful of
                 * separate disk writes for the bitmap, counters, pointer block and
                 * inode, whatever the size), so small chunks just waste it -- a
                 * measured 8KB round trip cost ~150 ms, the same as a big one.
                 * So grow the chunk (doubling) for as long as a round trip stays
                 * within the hitch limit, and halve it when one does not.
                 * (Only full chunks count -- the short tail of a file says
                 * nothing about speed.) */
                unsigned long long dt = FT_NOW() - c0; if (dt == 0) dt = 1;
                j->last_chunk_ms = dt;
                if ((unsigned)n == want_len) {
                    unsigned long long rate = (unsigned long long)n / dt;
                    j->rate_bpms = j->rate_bpms ? (j->rate_bpms * 3 + rate) / 4 : rate;
                    unsigned next = want_len;
                    if (dt > j->target_ms) { next = want_len / 2; if (next < FT_CHUNK_MIN) next = FT_CHUNK_MIN; }
                    else if (want_len < FT_CHUNK) { next = want_len * 2; if (next > FT_CHUNK) next = FT_CHUNK; }
                    j->chunk_len = next;
                }
            }
        } else if (j->depth < 0) {
            /* Nothing left to walk. End of the SCAN pass -> start the COPY
             * pass; end of the COPY pass -> finished. */
            if (j->state == FTJ_SCAN) { if (ftj_begin_copy() != 0) break; }
            else { j->state = FTJ_DONE; j->dst_created = 0; break; }
        } else {
            FtFrame* f = &j->fr[j->depth];
            if (f->next >= f->n) { j->depth--; }
            else {
                Dirent* e = &ft_ents[j->depth][f->next++];
                char s2[FT_PATH_MAX], d2[FT_PATH_MAX];
                if (ft_join(s2, sizeof s2, f->sp, e->name) != 0 || ft_join(d2, sizeof d2, f->dp, e->name) != 0) { ftj_fail("path too long"); break; }
                if (e->is_dir) {
                    if (ftj_push(s2, d2, j->state == FTJ_COPY) != 0) break;
                } else if (j->state == FTJ_SCAN) {
                    j->total_files++; j->total_bytes += e->size;
                } else {
                    ftj_set_name(e->name);
                    if (e->size == 0) {
                        if (sys_save_chunk(d2, ft_chunk, 0, 0) != 0) { ftj_fail("cannot create file"); break; }
                        j->done_files++;
                    } else {
                        j->fd = sys_open(s2, 0);
                        if (j->fd < 0) { ftj_fail("cannot open source file"); break; }
                        ftj_copy_str(j->fdst, d2, FT_PATH_MAX); j->off = 0;
                    }
                }
            }
        }
        if (++iters >= max_iters) break;        /* hard cap, independent of any clock */
        if (FT_NOW() - t0 >= budget) break;
    }
    if (moved > j->max_step_bytes) j->max_step_bytes = moved;
    return j->state;
}
/* Same, with no iteration cap. */
static int ft_job_step(unsigned long long budget) { return ft_job_step_n(budget, 0xFFFFFFFFu); }

/* 0..100 */
static int ft_job_percent(void) {
    FtJob* j = &ft_job;
    if (j->state == FTJ_DONE) return 100;
    if (j->state == FTJ_SCAN) return 0;
    if (j->kind == FTK_DELETE) {
        if (!j->total_known || j->total_items == 0) return 0;
        unsigned long long p = (j->done_items * 100ULL) / j->total_items;
        return p > 100 ? 100 : (int)p;
    }
    if (j->total_bytes) return (int)((j->done_bytes * 100ULL) / j->total_bytes);
    if (j->total_files) return (int)((j->done_files * 100ULL) / j->total_files);
    return 0;
}

#endif /* FS_TREE_H */
