/* Permanent regression test for sys_fseek()/sys_fsize() (kept like
 * crashtest/exectest/spawntest/iotest). Usage, from the desktop
 * terminal:   seektest /ycfs/some_big_file
 *             seektest /disk/FILE.BIN
 *
 * 1. Reads the whole file sequentially, hashing it in 64 KB blocks.
 * 2. Visits every block again in a scrambled order using sys_fseek(),
 *    and checks each block's hash matches pass 1.
 * 3. Unaligned probes (inside a block and straddling two blocks).
 * 4. SEEK_END / SEEK_CUR / clamp-past-EOF / negative-offset checks.
 * Prints PASS or the first failure, plus read throughput. */
#include "../lib/syscall.h"

#define BLK      65536u
#define MAX_BLKS 4096u            /* up to 256 MB */

static uint8_t   bufa[BLK], bufb[BLK];
static uint32_t hashes[MAX_BLKS];
static uint32_t checks = 0;

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void out(const char* s) { sys_write(1, s, (uint64_t)slen(s)); }
static void outnum(uint64_t v) {
    char t[24]; int i = 23; t[i] = 0;
    if (!v) t[--i] = '0';
    while (v) { t[--i] = (char)('0' + v % 10); v /= 10; }
    out(&t[i]);
}
static int fail(const char* what, uint64_t a, uint64_t b) {
    out("seektest: FAIL: "); out(what);
    out(" (got "); outnum(a); out(", expected "); outnum(b); out(")\n");
    return 1;
}
static uint32_t fnv(const uint8_t* p, uint32_t n) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
/* Read exactly n bytes unless EOF; returns bytes read. */
static uint32_t read_full(int64_t fd, uint8_t* dst, uint32_t n) {
    uint32_t got = 0;
    while (got < n) {
        int64_t r = sys_fread(fd, dst + got, n - got);
        if (r <= 0) break;
        got += (uint32_t)r;
    }
    return got;
}
static uint64_t gcd(uint64_t a, uint64_t b) { while (b) { uint64_t t = a % b; a = b; b = t; } return a; }

int main(void) {
    char path[128];
    int64_t al = sys_get_exec_arg(path, sizeof(path));
    if (al <= 0 || !path[0]) { out("usage: seektest <path>\n"); return 1; }

    int64_t fd = sys_open(path, 0);
    if (fd < 0) { out("seektest: cannot open file\n"); return 1; }
    int64_t sz = sys_fsize(fd);
    if (sz <= 0) { out("seektest: size unknown or zero\n"); return 1; }
    out("seektest: size "); outnum((uint64_t)sz); out(" bytes\n");
    uint64_t size = (uint64_t)sz;
    uint32_t nblk = (uint32_t)((size + BLK - 1) / BLK);
    if (nblk > MAX_BLKS) { out("seektest: file too big for test (max 256MB)\n"); return 1; }

    /* Pass 1: sequential */
    uint64_t t0 = sys_ticks();
    uint64_t total = 0;
    for (uint32_t b = 0; b < nblk; b++) {
        uint32_t want = (b == nblk - 1) ? (uint32_t)(size - (uint64_t)b * BLK) : BLK;
        uint32_t got = read_full(fd, bufa, want);
        if (got != want) return fail("sequential read short", got, want);
        hashes[b] = fnv(bufa, got);
        total += got;
    }
    uint64_t t1 = sys_ticks();
    if (total != size) return fail("sequential total", total, size);
    if (sys_fread(fd, bufa, 16) != 0) return fail("read at EOF should return 0", 1, 0);
    checks += 2;
    out("seektest: pass 1 (sequential) ok, ticks="); outnum(t1 - t0); out("\n");

    /* Pass 2: every block, scrambled order */
    uint64_t step = nblk * 5 / 8 + 1;
    while (gcd(step, nblk) != 1) step++;
    uint64_t t2 = sys_ticks();
    for (uint32_t i = 0; i < nblk; i++) {
        uint32_t b = (uint32_t)(((uint64_t)i * step) % nblk);
        uint64_t off = (uint64_t)b * BLK;
        int64_t r = sys_fseek(fd, (int64_t)off, SEEK_SET);
        if (r != (int64_t)off) return fail("fseek SET return", (uint64_t)r, off);
        uint32_t want = (b == nblk - 1) ? (uint32_t)(size - off) : BLK;
        uint32_t got = read_full(fd, bufa, want);
        if (got != want) return fail("random-block read short", got, want);
        if (fnv(bufa, got) != hashes[b]) return fail("random-block hash mismatch, block", b, b);
        checks++;
    }
    uint64_t t3 = sys_ticks();
    out("seektest: pass 2 (scrambled seeks, "); outnum(nblk); out(" blocks) ok, ticks=");
    outnum(t3 - t2); out("\n");

    /* Unaligned probes */
    for (uint32_t k = 0; k < 8; k++) {
        uint32_t b = (nblk > 1) ? (uint32_t)((k * 2654435761u) % (nblk - 1)) : 0;
        uint64_t base = (uint64_t)b * BLK;
        /* reference: aligned reads of blocks b and b+1 */
        uint32_t la = BLK, lb = 0;
        sys_fseek(fd, (int64_t)base, SEEK_SET);
        la = read_full(fd, bufa, BLK);
        if (b + 1 < nblk) lb = read_full(fd, bufb, BLK);
        uint32_t inb = 37 + k * 4099;
        if (inb >= la) inb = la ? la - 1 : 0;
        uint32_t len = 1000 + k * 997;
        if (inb + len > la + lb) len = la + lb - inb;
        /* probe 1: inside / straddling, via seek to an unaligned offset */
        int64_t r = sys_fseek(fd, (int64_t)(base + inb), SEEK_SET);
        if (r != (int64_t)(base + inb)) return fail("unaligned fseek return", (uint64_t)r, base + inb);
        static uint8_t probe[BLK];
        uint32_t want = len > BLK ? BLK : len;
        uint32_t got = read_full(fd, probe, want);
        if (got != want) return fail("unaligned read short", got, want);
        for (uint32_t i = 0; i < got; i++) {
            uint8_t ref = (inb + i < la) ? bufa[inb + i] : bufb[inb + i - la];
            if (probe[i] != ref) return fail("unaligned byte mismatch at", base + inb + i, base + inb + i);
        }
        checks++;
        /* probe 2: SEEK_CUR relative hop forward over `got` bytes */
        int64_t pos = sys_fseek(fd, 0, SEEK_CUR);
        if (pos != (int64_t)(base + inb + got)) return fail("SEEK_CUR 0", (uint64_t)pos, base + inb + got);
        checks++;
    }
    out("seektest: unaligned probes ok\n");

    /* End-relative and edge cases */
    int64_t r = sys_fseek(fd, 0, SEEK_END);
    if (r != (int64_t)size) return fail("SEEK_END 0", (uint64_t)r, size);
    if (sys_fread(fd, bufa, 16) != 0) return fail("read after SEEK_END", 1, 0);
    uint32_t tail = size < 100 ? (uint32_t)size : 100;
    r = sys_fseek(fd, -(int64_t)tail, SEEK_END);
    if (r != (int64_t)(size - tail)) return fail("SEEK_END -100", (uint64_t)r, size - tail);
    uint32_t got = read_full(fd, bufa, tail);
    if (got != tail) return fail("tail read short", got, tail);
    sys_fseek(fd, (int64_t)((uint64_t)(nblk - 1) * BLK), SEEK_SET);
    uint32_t lastlen = (uint32_t)(size - (uint64_t)(nblk - 1) * BLK);
    got = read_full(fd, bufb, lastlen);
    for (uint32_t i = 0; i < tail; i++)
        if (bufa[i] != bufb[lastlen - tail + i]) return fail("tail byte mismatch", i, i);
    r = sys_fseek(fd, 1000000000LL, SEEK_SET);
    if (r != (int64_t)size) return fail("seek past EOF clamps to size", (uint64_t)r, size);
    r = sys_fseek(fd, -1, SEEK_SET);
    if (r != -1) return fail("negative absolute offset must fail", (uint64_t)r, (uint64_t)-1);
    r = sys_fseek(fd, 0, 99);
    if (r != -1) return fail("bad whence must fail", (uint64_t)r, (uint64_t)-1);
    r = sys_fseek(fd, 0, SEEK_SET);
    if (r != 0) return fail("seek back to 0", (uint64_t)r, 0);
    got = read_full(fd, bufa, BLK < size ? BLK : (uint32_t)size);
    if (fnv(bufa, got) != hashes[0]) return fail("block 0 after rewind", 0, 0);
    checks += 8;
    out("seektest: edge cases ok\n");

    sys_close(fd);
    out("seektest: PASS ("); outnum(checks); out(" checks)\n");
    return 0;
}
