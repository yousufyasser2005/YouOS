/* txarena.h -- memory for the text editor. Userspace has no heap, so documents (text, undo history,
 * line index) are carved out of one static arena.
 *
 * Blocks come in power-of-two size classes from 4 KiB to 8 MiB. A freed block goes on the free list
 * of its class and is reused for the next request of that class. When a class has nothing free and
 * the arena has no untouched space left, a larger free block is split down to the size needed (the
 * halves go back on the lists), so memory freed by closing a big document is usable for small ones.
 * Nothing is merged back upward: simple, predictable, and enough for this use.
 *
 * txa_alloc(n) returns a block of txa_round(n) bytes (the CALLER must remember n or the rounded size
 * and pass the same value to txa_free) or 0 when the arena cannot satisfy the request. Memory is
 * NOT zeroed. */
#ifndef TXARENA_H
#define TXARENA_H

#ifndef TX_ARENA_BYTES
#define TX_ARENA_BYTES (24u * 1024u * 1024u)
#endif
#define TXA_MIN_SHIFT 12
#define TXA_MAX_SHIFT 23
#define TXA_CLASSES   (TXA_MAX_SHIFT - TXA_MIN_SHIFT + 1)

static unsigned char txa_mem[TX_ARENA_BYTES] __attribute__((aligned(4096)));
static unsigned long txa_bump = 0;
static void*         txa_lists[TXA_CLASSES];
static unsigned long txa_in_use = 0;                    /* bytes currently handed out (rounded) */

static int txa_class(unsigned long n) {                 /* -1 if too large */
    int c = 0; unsigned long s = 1ul << TXA_MIN_SHIFT;
    while (s < n) { s <<= 1; c++; if (c >= TXA_CLASSES) return -1; }
    return c;
}
static unsigned long txa_round(unsigned long n) { int c = txa_class(n); return c < 0 ? 0 : (1ul << (TXA_MIN_SHIFT + c)); }

static void* txa_alloc(unsigned long n) {
    if (n == 0) n = 1;
    int c = txa_class(n); if (c < 0) return 0;
    unsigned long size = 1ul << (TXA_MIN_SHIFT + c);
    void* p = 0;
    if (txa_lists[c]) { p = txa_lists[c]; txa_lists[c] = *(void**)p; }
    else if (txa_bump + size <= TX_ARENA_BYTES) { p = txa_mem + txa_bump; txa_bump += size; }
    else {
        for (int k = c + 1; k < TXA_CLASSES; k++) if (txa_lists[k]) {          /* split a bigger free block */
            p = txa_lists[k]; txa_lists[k] = *(void**)p;
            while (k > c) { k--; void* half = (char*)p + (1ul << (TXA_MIN_SHIFT + k)); *(void**)half = txa_lists[k]; txa_lists[k] = half; }
            break;
        }
    }
    if (p) txa_in_use += size;
    return p;
}
static void txa_free(void* p, unsigned long n) {
    if (!p) return;
    int c = txa_class(n ? n : 1); if (c < 0) return;
    *(void**)p = txa_lists[c]; txa_lists[c] = p;
    txa_in_use -= (1ul << (TXA_MIN_SHIFT + c));
}
#endif
