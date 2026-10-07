/* CMOS real-time clock (MC146818-compatible) -- ports 0x70/0x71.
 * Present on every PC-compatible machine (real hardware and QEMU), so
 * this driver needs no per-platform variant. */
#include <kernel/rtc.h>

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71
#define REG_SEC   0x00
#define REG_MIN   0x02
#define REG_HOUR  0x04
#define REG_DAY   0x07
#define REG_MONTH 0x08
#define REG_YEAR  0x09
#define REG_CENT  0x32   /* usual century register; not guaranteed */
#define REG_A     0x0A
#define REG_B     0x0B

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);        /* bit 7 clear: NMI left enabled */
    return inb(CMOS_DATA);
}

static int update_in_progress(void) {
    return cmos_read(REG_A) & 0x80;
}

typedef struct { uint8_t s, m, h, d, mo, y, c; } raw_t;

static void read_raw(raw_t* r) {
    r->s = cmos_read(REG_SEC);   r->m  = cmos_read(REG_MIN);
    r->h = cmos_read(REG_HOUR);  r->d  = cmos_read(REG_DAY);
    r->mo = cmos_read(REG_MONTH); r->y = cmos_read(REG_YEAR);
    r->c = cmos_read(REG_CENT);
}

static int raw_eq(const raw_t* a, const raw_t* b) {
    return a->s == b->s && a->m == b->m && a->h == b->h && a->d == b->d &&
           a->mo == b->mo && a->y == b->y && a->c == b->c;
}

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v & 0x0F) + (v >> 4) * 10); }

int rtc_read(rtc_time_t* out) {
    raw_t a, b;
    int guard = 0;

    /* The clock updates once a second; a read that straddles an update
     * can mix old and new fields. Wait for the update flag to clear,
     * then read twice and accept only when both snapshots agree. */
    do {
        int t = 100000;
        while (update_in_progress() && --t) {}
        read_raw(&a);
        t = 100000;
        while (update_in_progress() && --t) {}
        read_raw(&b);
    } while (!raw_eq(&a, &b) && ++guard < 5);
    if (!raw_eq(&a, &b)) return -1;

    uint8_t regb = cmos_read(REG_B);
    int binary = (regb & 0x04) != 0;
    int h24    = (regb & 0x02) != 0;

    uint8_t pm = a.h & 0x80;     /* in 12h mode bit 7 means PM */
    a.h &= 0x7F;
    if (!binary) {
        a.s = bcd2bin(a.s); a.m = bcd2bin(a.m); a.h = bcd2bin(a.h);
        a.d = bcd2bin(a.d); a.mo = bcd2bin(a.mo); a.y = bcd2bin(a.y);
        a.c = bcd2bin(a.c);
    }
    if (!h24) {                  /* 12h clock: 12 AM = 0, 12 PM = 12 */
        if (a.h == 12) a.h = 0;
        if (pm) a.h = (uint8_t)(a.h + 12);
    }

    /* Century: trust register 0x32 only if it looks like 19..21,
     * otherwise assume 20xx. */
    unsigned year = a.y;
    if (a.c >= 19 && a.c <= 21) year += (unsigned)a.c * 100;
    else                        year += 2000;

    if (a.mo < 1 || a.mo > 12 || a.d < 1 || a.d > 31 ||
        a.h > 23 || a.m > 59 || a.s > 59) return -1;

    out->year = (uint16_t)year; out->month = a.mo; out->day = a.d;
    out->hour = a.h; out->minute = a.m; out->second = a.s;
    return 0;
}

uint64_t rtc_debug(int mode) {
    if (mode == 0) {
        return (uint64_t)cmos_read(REG_A) | ((uint64_t)cmos_read(REG_B) << 8) |
               ((uint64_t)cmos_read(REG_SEC) << 16) | ((uint64_t)cmos_read(REG_MIN) << 24) |
               ((uint64_t)cmos_read(REG_HOUR) << 32) | ((uint64_t)cmos_read(REG_DAY) << 40) |
               ((uint64_t)cmos_read(REG_MONTH) << 48) | ((uint64_t)cmos_read(REG_YEAR) << 56);
    }
    rtc_time_t t;
    int rc = rtc_read(&t);
    return (uint64_t)cmos_read(REG_CENT) | ((uint64_t)(rc & 0xFF) << 8);
}
