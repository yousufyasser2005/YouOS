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
static long n_nf = 0, n_flush = 0; static int flush_pending = 0;
static long n_lock = 0, n_unlock = 0;
void storage_lock(void) { n_lock++; }
void storage_unlock(void) { n_unlock++; }

int ata_write_sectors_nf(uint64_t lba, uint8_t count, const void* buf) { n_nf++; flush_pending = 1; return ata_write_sectors(lba, count, buf); }
int ata_flush_now(void) { if (flush_pending) { n_flush++; flush_pending = 0; } return 0; }
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
    } else if (!strcmp(cmd, "readcount")) {     /* readcount NAME: read the whole file in 1MB pieces, report disk read commands used */
        uint32_t ino; uint64_t size;
        if (find(argv[3], &ino, &size)) return die("lookup failed");
        static uint8_t buf[1 << 20]; uint64_t off = 0; long r0 = n_r;
        while (off < size) {
            uint32_t want = size - off > sizeof buf ? sizeof buf : (uint32_t)(size - off);
            int64_t n = ycfs_read(ino, off, want, buf);
            if (n != want) return die("short read");
            off += n;
        }
        printf("readcount %s: %llu bytes in %ld read commands\n", argv[3], (unsigned long long)size, n_r - r0);
    } else if (!strcmp(cmd, "frag")) {          /* two files written alternately, block by block, so their blocks interleave on disk */
        uint32_t ia, ib;
        if (ycfs_create(root_ino(), "frag_a", YCFS_TYPE_FILE, &ia) != 0 || ycfs_create(root_ino(), "frag_b", YCFS_TYPE_FILE, &ib) != 0) return die("create");
        static uint8_t blk[4096], big[200 * 4096];
        for (int i = 0; i < 200; i++) {
            memset(blk, (uint8_t)(i * 3 + 1), sizeof blk); if (ycfs_write(ia, (uint64_t)i * 4096, 4096, blk) != 4096) return die("write a");
            memset(blk, (uint8_t)(i * 5 + 2), sizeof blk); if (ycfs_write(ib, (uint64_t)i * 4096, 4096, blk) != 4096) return die("write b");
        }
        long r0 = n_r; int ok = 1;
        for (int f = 0; f < 2; f++) {
            memset(big, 0, sizeof big);
            int64_t n = ycfs_read(f ? ib : ia, 0, sizeof big, big);
            if (n != (int64_t)sizeof big) ok = 0;
            for (int i = 0; ok && i < 200; i++) {
                uint8_t want = (uint8_t)(f ? i * 5 + 2 : i * 3 + 1);
                for (int k = 0; k < 4096; k++) if (big[(size_t)i * 4096 + k] != want) { ok = 0; break; }
            }
        }
        /* and an unaligned read that starts and ends inside blocks, across many gaps */
        static uint8_t part[3 * 4096 + 777]; int64_t n = ycfs_read(ia, 4096 * 10 + 100, sizeof part, part);
        if (n != (int64_t)sizeof part) ok = 0;
        for (size_t k = 0; ok && k < sizeof part; k++) { uint64_t o = 4096ull * 10 + 100 + k; uint8_t want = (uint8_t)((o / 4096) * 3 + 1); if (part[k] != want) ok = 0; }
        printf("frag: %s, %ld read commands for two interleaved 200-block files\n", ok ? "PASS" : "FAIL", n_r - r0);
        if (!ok) return 1;
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
    fprintf(stderr, "[io: %ld reads %ld writes, highest LBA %llu, %ld unflushed data writes]\n", n_r, n_w, (unsigned long long)max_lba, n_nf);
    return 0;
}
