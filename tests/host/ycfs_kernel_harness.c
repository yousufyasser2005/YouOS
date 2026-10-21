/* Host harness: runs the REAL kernel/fs/ycfs.c against a disk image. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <kernel/ycfs.h>

static int fd; static uint64_t max_lba = 0; static long n_r = 0, n_w = 0;
int ata_read_sectors(uint64_t lba, uint8_t count, void* buf) {
    if (lba + count > max_lba) max_lba = lba + count;
    n_r++; return pread(fd, buf, (size_t)count * 512, (off_t)(lba * 512)) == (ssize_t)count * 512 ? 0 : -1;
}
int ata_write_sectors(uint64_t lba, uint8_t count, const void* buf) {
    if (lba + count > max_lba) max_lba = lba + count;
    n_w++; return pwrite(fd, buf, (size_t)count * 512, (off_t)(lba * 512)) == (ssize_t)count * 512 ? 0 : -1;
}
int session_lookup(uint32_t* u, uint32_t* g) { *u = 0; *g = 0; return 0; }
int session_is_elevated(void) { return 1; }
void vga_puts_color(const char* s, int a, int b) { (void)a; (void)b; fputs(s, stderr); }

static uint32_t root_ino(void) { return ycfs_root_inode(); }
static int find(const char* name, uint32_t* ino, uint64_t* size) {
    uint32_t t; return ycfs_lookup(root_ino(), name, ino, &t, size);
}
static int die(const char* m) { fprintf(stderr, "HARNESS FAIL: %s\n", m); return 1; }

int main(int argc, char** argv) {
    fd = open(argv[1], O_RDWR); if (fd < 0) { perror("open"); return 1; }
    if (ycfs_init() != 0) return die("ycfs_init failed");
    const char* cmd = argv[2];
    if (!strcmp(cmd, "read")) {                 /* read NAME OUTFILE  (chunked, via ycfs_read) */
        uint32_t ino; uint64_t size;
        if (find(argv[3], &ino, &size)) return die("lookup failed");
        FILE* o = fopen(argv[4], "wb"); static uint8_t buf[1 << 20]; uint64_t off = 0;
        while (off < size) {
            uint32_t want = size - off > sizeof buf ? sizeof buf : (uint32_t)(size - off);
            int64_t n = ycfs_read(ino, off, want, buf);
            if (n != want) { fprintf(stderr, "short read at %llu: %lld of %u\n", (unsigned long long)off, (long long)n, want); return 1; }
            fwrite(buf, 1, n, o); off += n;
        }
        fclose(o); printf("read %s: %llu bytes\n", argv[3], (unsigned long long)size);
    } else if (!strcmp(cmd, "pread")) {         /* pread NAME OFFSET LEN OUTFILE (arbitrary offset) */
        uint32_t ino; uint64_t size;
        if (find(argv[3], &ino, &size)) return die("lookup failed");
        uint64_t off = strtoull(argv[4], 0, 0); uint32_t len = (uint32_t)strtoul(argv[5], 0, 0);
        uint8_t* b = malloc(len); int64_t n = ycfs_read(ino, off, len, b);
        FILE* o = fopen(argv[6], "wb"); fwrite(b, 1, n > 0 ? n : 0, o); fclose(o); printf("pread %lld\n", (long long)n);
    } else if (!strcmp(cmd, "write")) {         /* write NAME SRCFILE  (create + chunked ycfs_write) */
        FILE* s = fopen(argv[4], "rb"); if (!s) return die("src");
        uint32_t ino;
        if (ycfs_create(root_ino(), argv[3], YCFS_TYPE_FILE, &ino) != 0) return die("create failed");
        static uint8_t buf[1 << 20]; uint64_t off = 0; size_t n;
        while ((n = fread(buf, 1, sizeof buf, s)) > 0) {
            int64_t w = ycfs_write(ino, off, (uint32_t)n, buf);
            if (w != (int64_t)n) { fprintf(stderr, "short write at %llu: %lld of %zu\n", (unsigned long long)off, (long long)w, n); return 1; }
            off += n;
        }
        printf("wrote %s: %llu bytes\n", argv[3], (unsigned long long)off);
    } else if (!strcmp(cmd, "unlink")) {
        char p[80]; snprintf(p, sizeof p, "/%s", argv[3]);
        if (ycfs_unlink(p) != 0) return die("unlink failed");
        printf("unlinked %s\n", argv[3]);
    } else if (!strcmp(cmd, "script")) {        /* script FILE: many operations in ONE mount (caches persist, like the real kernel) */
        FILE* sc = fopen(argv[3], "r"); char line[512]; int errs = 0;
        static uint8_t sbuf[1 << 22];
        while (fgets(line, sizeof line, sc)) {
            char op[32], a1[200], a2[200], a3[64]; a1[0] = a2[0] = a3[0] = 0;
            int n = sscanf(line, "%31s %199s %199s %63s", op, a1, a2, a3); if (n < 1) continue;
            if (!strcmp(op, "save")) {               /* save PATH SRCFILE */
                FILE* f = fopen(a2, "rb"); size_t m = fread(sbuf, 1, sizeof sbuf, f); fclose(f);
                int64_t w = ycfs_savefile(a1, sbuf, (uint32_t)m); if (w != (int64_t)m) { printf("FAIL save %s\n", a1); errs++; }
            } else if (!strcmp(op, "chunkfile")) {   /* chunkfile PATH SRCFILE CHUNKSIZE: copy in pieces via ycfs_write_chunk */
                FILE* f = fopen(a2, "rb"); uint32_t cs = (uint32_t)strtoul(a3, 0, 0); uint64_t off = 0; size_t m;
                static uint8_t cb[1 << 20]; if (cs > sizeof cb) cs = sizeof cb;
                int any = 0;
                while ((m = fread(cb, 1, cs, f)) > 0) { any = 1;
                    if (ycfs_write_chunk(a1, off, cb, (uint32_t)m) != (int64_t)m) { printf("FAIL chunk %s at %llu\n", a1, (unsigned long long)off); errs++; break; }
                    off += m; }
                if (!any && ycfs_write_chunk(a1, 0, cb, 0) != 0) { printf("FAIL empty chunk %s\n", a1); errs++; }
                fclose(f);
            } else if (!strcmp(op, "read")) {        /* read PATH OUTFILE */
                int64_t m = ycfs_read_file(a1, sbuf, sizeof sbuf);
                if (m < 0) { printf("FAIL read %s\n", a1); errs++; m = 0; }
                FILE* o = fopen(a2, "wb"); fwrite(sbuf, 1, m, o); fclose(o);
            } else if (!strcmp(op, "unlink"))  { if (ycfs_unlink(a1)) { printf("FAIL unlink %s\n", a1); errs++; } }
            else if (!strcmp(op, "unlink_must_fail")) { if (!ycfs_unlink(a1)) { printf("FAIL: unlink %s should have been refused\n", a1); errs++; } }
            else if (!strcmp(op, "mkdir"))     { if (ycfs_mkdir(a1)) { printf("FAIL mkdir %s\n", a1); errs++; } }
            else if (!strcmp(op, "rename"))    { if (ycfs_rename(a1, a2)) { printf("FAIL rename %s %s\n", a1, a2); errs++; } }
            else if (!strcmp(op, "rename_must_fail")) { if (!ycfs_rename(a1, a2)) { printf("FAIL: rename %s %s should have been refused\n", a1, a2); errs++; } }
            else if (!strcmp(op, "size")) {          /* size PATH EXPECTED */
                uint32_t sz; uint8_t d; if (ycfs_stat(a1, &sz, &d)) { printf("FAIL stat %s\n", a1); errs++; }
                else if (sz != (uint32_t)strtoul(a2, 0, 0)) { printf("FAIL size %s = %u, expected %s\n", a1, sz, a2); errs++; } }
        }
        printf("script done, errors=%d\n", errs); if (errs) return 1;
    } else if (!strcmp(cmd, "journaltest")) {
        int r = ycfs_journal_self_test(); printf("journal self test: %s\n", r == 0 ? "PASS" : "FAIL"); if (r) return 1;
    } else return die("unknown command");
    fprintf(stderr, "[io: %ld reads %ld writes, highest LBA %llu]\n", n_r, n_w, (unsigned long long)max_lba);
    return 0;
}
