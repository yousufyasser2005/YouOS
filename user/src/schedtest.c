/* Permanent in-OS regression test for the blocking-I/O foundation (storage lock, interrupt-driven
 * disk waits, deferred kill, lock fairness). Run from the desktop terminal:   schedtest
 *
 * It starts helper processes of THIS program and watches them:
 *   A, B  two WRITERS, each writing 8 MB of a position-dependent pattern with sys_save_chunk(),
 *         then reading it back and comparing every byte. They run at the same time, so their
 *         disk operations interleave at every sleep point.
 *   T     a SCHEDULER PROBE: sleeps one tick, wakes, repeats, and records the longest gap between
 *         wake-ups while the disk is busy. It never touches the filesystem. If a disk operation
 *         froze the machine the way it used to, the gap would be as long as one chunk write.
 *   F     a FILESYSTEM PROBE: once a tick it calls sys_stat() on a file, i.e. it needs the storage
 *         lock while the writers are hammering it, and records the longest time one call took.
 *         This is the lock-fairness check: a lock that lets its last owner take it straight back
 *         starves this process for seconds.
 *   V     a VICTIM writer that is KILLED in the middle of its write. The kill must not leave the
 *         filesystem lock held, must not corrupt anything, and must not stop the others.
 * The helpers report through a message queue (not files), so the measurements themselves do not
 * depend on the filesystem. Afterwards it checks the filesystem still works. Run
 * `python3 tools/ycfs.py fsck disk.img` after shutting QEMU down for the on-disk verdict. */
#include "../lib/syscall.h"

#define CHUNK     (256u * 1024u)
#define WR_MB     8u
#define VICTIM_MB 64u
#define PATH_A    "/ycfs/st_a.bin"
#define PATH_B    "/ycfs/st_b.bin"
#define PATH_V    "/ycfs/st_v.bin"
#define SMALL     "/ycfs/st_small.txt"
#define QUEUE     "st_res"

typedef struct { char who; char pad[3]; char text[120]; } Report;      /* 124 bytes */

static uint8_t buf[CHUNK];

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void out(const char* s) { sys_write(1, s, (uint64_t)slen(s)); }
static void outnum(uint64_t v) {
    char t[24]; int i = 23; t[i] = 0;
    if (!v) t[--i] = '0';
    while (v) { t[--i] = (char)('0' + v % 10); v /= 10; }
    out(&t[i]);
}
static int cat(char* d, int k, const char* s) { while (*s) d[k++] = *s++; d[k] = 0; return k; }
static int catnum(char* d, int k, uint64_t v) {
    char t[24]; int i = 23; t[i] = 0;
    if (!v) t[--i] = '0';
    while (v) { t[--i] = (char)('0' + v % 10); v /= 10; }
    return cat(d, k, &t[i]);
}
static int starts(const char* s, const char* p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }
static uint64_t num_after(const char* s, const char* key) {
    int kl = slen(key);
    for (; *s; s++) if (starts(s, key)) { s += kl; uint64_t v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (uint64_t)(*s++ - '0'); return v; }
    return 0;
}
static void report(char who, const char* text) {
    Report r; char* p = (char*)&r; for (unsigned i = 0; i < sizeof r; i++) p[i] = 0;
    r.who = who; int k = 0; while (text[k] && k < 119) { r.text[k] = text[k]; k++; }
    sys_msgpost(QUEUE, &r, (unsigned)sizeof r);
}

static inline uint8_t pat(uint32_t seed, uint64_t o) {
    uint32_t x = (uint32_t)o * 2654435761u + seed * 40503u;
    return (uint8_t)((x >> 15) ^ (x >> 7) ^ seed);
}
static void fill(uint32_t seed, uint64_t off, uint32_t n) { for (uint32_t i = 0; i < n; i++) buf[i] = pat(seed, off + i); }

/* ---- helper roles ---- */
static int role_writer(char who, const char* path, uint32_t seed, uint32_t mb, int verify) {
    uint64_t total = (uint64_t)mb * 1024 * 1024;
    uint64_t t0 = sys_ticks();
    for (uint64_t off = 0; off < total; off += CHUNK) {
        fill(seed, off, CHUNK);
        if (sys_save_chunk(path, buf, CHUNK, off) != (int64_t)CHUNK) { if (verify) report(who, "FAIL write error"); return 1; }
    }
    uint64_t t1 = sys_ticks();
    if (!verify) return 0;
    int fd = sys_open(path, 0);
    if (fd < 0) { report(who, "FAIL cannot reopen file"); return 1; }
    uint64_t bad = (uint64_t)-1;
    for (uint64_t off = 0; off < total && bad == (uint64_t)-1; off += CHUNK) {
        uint32_t got = 0;
        while (got < CHUNK) { int64_t n = sys_fread(fd, buf + got, CHUNK - got); if (n <= 0) break; got += (uint32_t)n; }
        if (got != CHUNK) { bad = off + got; break; }
        for (uint32_t i = 0; i < CHUNK; i++) if (buf[i] != pat(seed, off + i)) { bad = off + i; break; }
    }
    sys_close(fd);
    uint64_t t2 = sys_ticks();
    char r[120]; int k = 0;
    if (bad != (uint64_t)-1) { k = cat(r, k, "FAIL data differs at offset "); catnum(r, k, bad); }
    else { k = cat(r, k, "PASS bytes="); k = catnum(r, k, total); k = cat(r, k, " write_ticks="); k = catnum(r, k, t1 - t0); k = cat(r, k, " verify_ticks="); catnum(r, k, t2 - t1); }
    report(who, r);
    return bad == (uint64_t)-1 ? 0 : 1;
}

static int role_sched_probe(void) {                    /* runs until killed; never touches the filesystem */
    uint64_t last = sys_ticks(), max_gap = 0, iters = 0, slow = 0;
    for (;;) {
        sys_sleep(1);
        uint64_t now = sys_ticks(), gap = now - last; last = now; iters++;
        if (gap > max_gap) max_gap = gap;
        if (gap > 3) slow++;
        if (iters % 20 == 0) {
            char r[120]; int k = 0;
            k = cat(r, k, "wakeups="); k = catnum(r, k, iters);
            k = cat(r, k, " max_gap_ticks="); k = catnum(r, k, max_gap);
            k = cat(r, k, " gaps_over_3_ticks="); catnum(r, k, slow);
            report('T', r);
        }
    }
}

static int role_fs_probe(void) {                       /* needs the storage lock once a tick; runs until killed */
    uint64_t calls = 0, max_lat = 0, over = 0;
    for (;;) {
        sys_sleep(1);
        unsigned int sz; unsigned char d;
        uint64_t t0 = sys_ticks();
        sys_stat(PATH_A, &sz, &d);
        uint64_t lat = sys_ticks() - t0; calls++;
        if (lat > max_lat) max_lat = lat;
        if (lat > 30) over++;
        if (calls % 5 == 0) {
            char r[120]; int k = 0;
            k = cat(r, k, "stats="); k = catnum(r, k, calls);
            k = cat(r, k, " max_latency_ticks="); k = catnum(r, k, max_lat);
            k = cat(r, k, " calls_over_300ms="); catnum(r, k, over);
            report('F', r);
        }
    }
}

/* ---- main ---- */
static int step_fail(const char* what) { out("schedtest: FAIL: "); out(what); out("\n"); return 1; }
static void note(char* slot, const Report* r) { int k = 0; while (r->text[k] && k < 119) { slot[k] = r->text[k]; k++; } slot[k] = 0; }
static void drain(char* a, char* b, char* t, char* f) {
    Report r; unsigned l, from;
    while (sys_msgrecv(QUEUE, &r, &l, &from) == 0) {
        if (r.who == 'A') note(a, &r); else if (r.who == 'B') note(b, &r);
        else if (r.who == 'T') note(t, &r); else if (r.who == 'F') note(f, &r);
    }
}
static int reap(int64_t pid, uint64_t max_ticks) {
    uint64_t t0 = sys_ticks();
    while (sys_ticks() - t0 < max_ticks) { if (sys_wait_nonblock(pid) != -2) return 1; sys_sleep(2); }
    return 0;
}

int main(void) {
    char arg[96]; arg[0] = 0;
    sys_get_exec_arg(arg, sizeof arg);
    if (arg[0] == 'W') return role_writer(arg[2], arg[2] == 'A' ? PATH_A : PATH_B, arg[2] == 'A' ? 7u : 11u, WR_MB, 1);
    if (arg[0] == 'V') return role_writer('V', PATH_V, 13u, VICTIM_MB, 0);
    if (arg[0] == 'T') return role_sched_probe();
    if (arg[0] == 'F') return role_fs_probe();

    out("schedtest: 2 verified writers + scheduler probe + filesystem probe + a writer killed mid-write\n");
    sys_unlink(PATH_A); sys_unlink(PATH_B); sys_unlink(PATH_V); sys_unlink(SMALL);
    sys_mqcreate(QUEUE);
    { Report r; unsigned l, f; while (sys_msgrecv(QUEUE, &r, &l, &f) == 0) { } }

    char ra[121], rb[121], rt[121], rf[121]; ra[0] = rb[0] = rt[0] = rf[0] = 0;
    uint64_t t_start = sys_ticks();
    int64_t pa = sys_spawn_arg("schedtest", "W A");
    int64_t pb = sys_spawn_arg("schedtest", "W B");
    int64_t pt = sys_spawn_arg("schedtest", "T");
    int64_t pf = sys_spawn_arg("schedtest", "F");
    int64_t pv = sys_spawn_arg("schedtest", "V");
    if (pa < 0 || pb < 0 || pt < 0 || pf < 0 || pv < 0) return step_fail("could not start the helper processes");

    sys_sleep(120);                                    /* ~1.2 s: let the victim get well into its write */
    int64_t kr = sys_kill(pv);
    uint64_t kill_at = sys_ticks();
    out("schedtest: killed the victim writer mid-write (sys_kill returned "); outnum((uint64_t)(kr < 0 ? -kr : kr)); out(")\n");

    int victim_reaped = 0; uint64_t victim_gone_after = 0;
    while (sys_ticks() - t_start < 6000) {             /* 60 s ceiling; this loop never touches the filesystem */
        if (!victim_reaped && sys_wait_nonblock(pv) != -2) { victim_reaped = 1; victim_gone_after = sys_ticks() - kill_at; }
        drain(ra, rb, rt, rf);
        if (ra[0] && rb[0] && victim_reaped) break;
        sys_sleep(5);
    }
    drain(ra, rb, rt, rf);
    uint64_t t_total = sys_ticks() - t_start;

    sys_kill(pt); sys_kill(pf);                        /* the probes run until stopped */
    int ok_t = reap(pt, 500), ok_f = reap(pf, 500);
    int ok_a = reap(pa, 500), ok_b = reap(pb, 500);
    drain(ra, rb, rt, rf);

    int fails = 0;
    out("  writer A      : "); out(ra[0] ? ra : "(no result)"); out("\n"); if (!starts(ra, "PASS")) fails++;
    out("  writer B      : "); out(rb[0] ? rb : "(no result)"); out("\n"); if (!starts(rb, "PASS")) fails++;
    out("  sched probe   : "); out(rt[0] ? rt : "(no result)"); out("\n");
    out("  fs probe      : "); out(rf[0] ? rf : "(no result)"); out("\n");
    out("  victim        : "); out(victim_reaped ? "reaped " : "NOT reaped, "); outnum(victim_gone_after * 10); out(" ms after the kill\n");
    out("  total         : "); outnum(t_total * 10); out(" ms\n");

    uint64_t t_wakes = num_after(rt, "wakeups="), t_gap = num_after(rt, "max_gap_ticks=");
    uint64_t f_calls = num_after(rf, "stats="), f_lat = num_after(rf, "max_latency_ticks=");
    if (!victim_reaped) { out("  FAIL: the killed writer never went away\n"); fails++; }
    if (!ok_t || !ok_f || !ok_a || !ok_b) { out("  FAIL: a helper process could not be stopped/reaped\n"); fails++; }
    if (t_wakes < 20) { out("  FAIL: the scheduler probe barely ran\n"); fails++; }
    else if (t_gap > 5) { out("  FAIL: scheduler probe saw a gap over 50 ms while the disk was busy\n"); fails++; }
    if (f_calls < 5) { out("  FAIL: the filesystem probe barely ran (starved for the storage lock?)\n"); fails++; }
    else if (f_lat > 100) { out("  FAIL: a sys_stat() waited over 1 s for the storage lock (unfair lock?)\n"); fails++; }

    sys_save_chunk(SMALL, "filesystem alive after the kill", 30, 0);
    int fd = sys_open(SMALL, 0); char tmp[40]; tmp[0] = 0;
    if (fd >= 0) { int64_t n = sys_fread(fd, tmp, 39); sys_close(fd); tmp[n > 0 ? n : 0] = 0; }
    int fs_ok = starts(tmp, "filesystem alive");
    out("  fs after      : "); out(fs_ok ? "read/write OK\n" : "BROKEN\n");
    if (!fs_ok) fails++;

    sys_unlink(PATH_A); sys_unlink(PATH_B); sys_unlink(PATH_V); sys_unlink(SMALL);
    out(fails ? "schedtest: FAIL\n" : "schedtest: PASS\n");
    return fails ? 1 : 0;
}
