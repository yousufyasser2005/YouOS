/* Host test for kernel/clipboard.c (the real file) and the key-event packing in include/kernel/keyev.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <kernel/clipboard.h>
#include <kernel/keyev.h>

static int failures = 0, checks = 0, live = 0, fail_next_alloc = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

void* kmalloc(size_t n) { if (fail_next_alloc) { fail_next_alloc = 0; return 0; } live++; return malloc(n); }
void  kfree(void* p)    { if (p) { live--; free(p); } }

int main(void) {
    uint32_t len, type, seq; char buf[64];
    printf("== system clipboard (kernel/clipboard.c)\n");
    clip_info(&len, &type, &seq);
    expect(len == 0 && seq == 0 && clip_get(buf, sizeof buf) == 0, "starts empty");

    int64_t s1 = clip_set("hello", 5, CLIP_TEXT);
    clip_info(&len, &type, &seq);
    expect(s1 == 1 && len == 5 && type == CLIP_TEXT && seq == 1, "set: sequence number, length and type recorded");
    memset(buf, '#', sizeof buf);
    expect(clip_get(buf, sizeof buf) == 5 && !memcmp(buf, "hello", 5) && buf[5] == '#', "get returns the full length and copies exactly that much");
    memset(buf, '#', sizeof buf);
    expect(clip_get(buf, 3) == 5 && !memcmp(buf, "hel", 3) && buf[3] == '#', "get with a small buffer: reports the FULL length, copies only what fits");

    int64_t s2 = clip_set("world!!", 7, CLIP_TEXT);
    expect(s2 == 2 && live == 1, "overwrite: new sequence number, old buffer freed (no leak)");
    memset(buf, 0, sizeof buf); clip_get(buf, sizeof buf);
    expect(!memcmp(buf, "world!!", 7), "overwrite: new contents");

    char* big = malloc(CLIP_MAX + 1); for (uint32_t i = 0; i < CLIP_MAX + 1; i++) big[i] = (char)(i * 31 + 7);
    expect(clip_set(big, CLIP_MAX + 1, CLIP_TEXT) == -1, "larger than the limit is refused");
    clip_info(&len, &type, &seq);
    memset(buf, 0, sizeof buf); clip_get(buf, sizeof buf);
    expect(len == 7 && seq == 2 && !memcmp(buf, "world!!", 7), "...and the old contents and sequence number are untouched");

    expect(clip_set(big, CLIP_MAX, CLIP_TEXT) == 3, "exactly the limit (4 MiB) is accepted");
    char* back = malloc(CLIP_MAX); int64_t n = clip_get(back, CLIP_MAX);
    expect(n == CLIP_MAX && !memcmp(back, big, CLIP_MAX), "4 MiB round-trips byte for byte");
    free(back);

    fail_next_alloc = 1;
    expect(clip_set("new", 3, CLIP_TEXT) == -1, "out of memory: set fails");
    clip_info(&len, &type, &seq);
    expect(len == CLIP_MAX && seq == 3 && live == 1, "...the old contents survive and nothing leaks");

    const char bin[6] = { 'a', 0, 'b', (char)0xFF, 0, 'c' };
    clip_set(bin, 6, 5);
    memset(buf, 0, sizeof buf); clip_info(&len, &type, &seq);
    expect(clip_get(buf, sizeof buf) == 6 && !memcmp(buf, bin, 6) && type == 5, "binary data with NUL bytes is kept as is; the type tag is kept");

    int64_t sc = clip_set(0, 0, 0);
    clip_info(&len, &type, &seq);
    expect(sc == seq && len == 0 && live == 0 && clip_get(buf, sizeof buf) == 0, "clear: empty, sequence number advances, memory released");
    free(big);

    printf("== key events (keyev.h)\n");
    uint64_t e = keyev_pack(0x2E, 'c', 0, 1, 0, 0);
    expect((e & 0xFFFF) == 'c' && (e & KEYEV_CTRL) && !(e & KEYEV_SHIFT) && (e & KEYEV_VALID), "Ctrl+C arrives as 'c' with the Ctrl flag (sys_keypoll lost it)");
    e = keyev_pack(0x2E, 'C', 1, 1, 0, 0);
    expect((e & 0xFFFF) == 'C' && (e & KEYEV_CTRL) && (e & KEYEV_SHIFT), "Ctrl+Shift+C: both flags");
    e = keyev_pack(0x4B, 0, 1, 0, 0, 0);
    expect((e & 0xFFFF) == 1003 && (e & KEYEV_SHIFT) && ((e >> 16) & 0xFF) == 0x4B, "Shift+Left: arrow code with the Shift flag (selection needs this)");
    uint32_t arrows[9][2] = {{0x48,1001},{0x50,1002},{0x4B,1003},{0x4D,1004},{0x47,1005},{0x4F,1006},{0x53,1007},{0x49,1008},{0x51,1009}};
    int ok = 1; for (int i = 0; i < 9; i++) if ((keyev_pack((uint8_t)arrows[i][0], '8', 0, 0, 0, 0) & 0xFFFF) != arrows[i][1]) ok = 0;
    expect(ok, "arrow/home/end/delete/page codes match sys_keypoll even when the scancode also yields a digit");
    e = keyev_pack(0x3B, 0, 0, 0, 0, 0);
    expect(e != 0 && (e & 0xFFFF) == 0 && (e & KEYEV_VALID), "a key with no code (F1) is still a real event, distinguishable from 'nothing'");
    e = keyev_pack(0x1E, 'a', 0, 0, 1, 1);
    expect((e & KEYEV_ALT) && (e & KEYEV_CAPS) && !(e & KEYEV_CTRL), "Alt and Caps Lock flags");

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
