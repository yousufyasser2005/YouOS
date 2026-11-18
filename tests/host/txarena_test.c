#define TX_ARENA_BYTES (1u * 1024u * 1024u)      /* small arena so exhaustion is easy to reach */
#include <stdio.h>
#include <string.h>
#include "../../user/src/txarena.h"
static int failures = 0, checks = 0;
#define expect(c, msg) do { checks++; if (c) printf("  ok    %s\n", msg); else { printf("  FAIL  %s\n", msg); failures++; } } while (0)
int main(void) {
    printf("== editor memory arena (txarena.h)\n");
    expect(txa_round(1) == 4096 && txa_round(4096) == 4097 - 1 && txa_round(4097) == 8192 && txa_round(1u << 23) == (1u << 23) && txa_round((1u << 23) + 1) == 0, "size classes: 4 KiB .. 8 MiB, powers of two, larger refused");
    void* a = txa_alloc(5000); void* b = txa_alloc(100000);
    expect(a && b && ((unsigned long)a % 4096 == 0) && ((unsigned long)b % 4096 == 0), "blocks are page aligned");
    memset(a, 0xAA, 8192); memset(b, 0xBB, 131072);
    expect(((unsigned char*)a)[8191] == 0xAA && ((unsigned char*)b)[0] == 0xBB && (char*)a + 8192 <= (char*)b, "blocks do not overlap");
    unsigned long used = txa_in_use; txa_free(a, 5000);
    expect(txa_in_use == used - 8192, "free returns the rounded size to the accounting");
    void* a2 = txa_alloc(6000);
    expect(a2 == a, "a freed block is reused for the same class");
    txa_free(a2, 6000); txa_free(b, 100000);
    expect(txa_in_use == 0, "everything freed: nothing in use");
    /* exhaust the untouched space with small blocks, then free a big one and carve small ones from it */
    void* big = txa_alloc(512 * 1024);
    void* small[300]; int n = 0; while (n < 300 && (small[n] = txa_alloc(4096))) n++;
    expect(txa_alloc(4096) == 0 && n > 0 && n < 300, "arena exhausted: further requests fail instead of corrupting");
    txa_free(big, 512 * 1024);
    void* s1 = txa_alloc(4096); void* s2 = txa_alloc(4096); void* s3 = txa_alloc(65536);
    expect(s1 && s2 && s3 && s1 != s2 && (char*)s1 >= (char*)big && (char*)s1 < (char*)big + 512 * 1024, "a freed big block is split to serve small requests");
    memset(s1, 1, 4096); memset(s2, 2, 4096); memset(s3, 3, 65536);
    expect(((unsigned char*)s1)[4095] == 1 && ((unsigned char*)s2)[0] == 2 && ((unsigned char*)s3)[65535] == 3, "split pieces are independent");
    expect(txa_alloc((1ul << 23) + 1) == 0 && txa_alloc(1ul << 23) == 0, "request bigger than the arena fails cleanly");
    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
