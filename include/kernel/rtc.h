#ifndef KERNEL_RTC_H
#define KERNEL_RTC_H
#include <stdint.h>

/* Broken-down wall-clock time as stored in the CMOS real-time clock.
 * The CMOS holds whatever the firmware/OS last wrote: on most PCs and in
 * QEMU that is local time (QEMU: host clock, -rtc base=utc by default
 * means UTC). YouOS displays it as-is, with no timezone conversion. */
typedef struct {
    uint16_t year;   /* full year, e.g. 2026 */
    uint8_t  month;  /* 1-12 */
    uint8_t  day;    /* 1-31 */
    uint8_t  hour;   /* 0-23 */
    uint8_t  minute; /* 0-59 */
    uint8_t  second; /* 0-59 */
} rtc_time_t;

/* Reads a consistent snapshot of the CMOS clock. Returns 0 on success,
 * -1 if the values read are implausible (dead/absent RTC). */
int rtc_read(rtc_time_t* out);

/* Diagnostic: raw CMOS bytes, packed little-endian as
 * A | B<<8 | sec<<16 | min<<24 | hour<<32 | day<<40 | month<<48 | year<<56
 * (mode 0), or century | (rtc_read() return code & 0xFF)<<8 (mode 1). */
uint64_t rtc_debug(int mode);

#endif
