#ifndef KERNEL_CLIPBOARD_H
#define KERNEL_CLIPBOARD_H
#include <stdint.h>

/*
 * System clipboard: one buffer shared by every process, held by the kernel so any program can copy
 * and paste (the desktop, terminals, scripts, future applications) -- not only the code that
 * happens to live in the desktop process.
 *
 *   clip_set(data, len, type)   replace the contents; len 0 clears it. Returns the new sequence
 *                               number, or -1 if len exceeds CLIP_MAX or memory is short -- in
 *                               which case the OLD contents are left untouched.
 *   clip_get(buf, max)          copy up to max bytes out; returns the FULL length (so a caller
 *                               can tell it was truncated and retry with a bigger buffer).
 *   clip_info(...)              length, type and sequence number without copying anything. The
 *                               sequence number changes on every set, so "did the clipboard
 *                               change?" is one cheap call.
 *
 * `type` tags what the bytes are, so images or file lists can be added later without a new
 * interface. CLIP_TEXT = UTF-8/ASCII text. All calls are atomic (system calls do not interleave).
 */
#define CLIP_MAX   (4u * 1024u * 1024u)   /* comfortably above the largest editable document (1 MiB) */
#define CLIP_TEXT  1u

int64_t clip_set(const void* data, uint32_t len, uint32_t type);
int64_t clip_get(void* buf, uint32_t max);
void    clip_info(uint32_t* len, uint32_t* type, uint32_t* seq);

#endif
