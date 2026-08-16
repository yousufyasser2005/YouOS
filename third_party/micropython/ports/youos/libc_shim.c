// libc_shim.c — minimal freestanding string/mem functions for MicroPython
// on YouOS.
//
// YouOS's userspace build is -nostdlib -ffreestanding: none of these exist.
// This is the ONLY libc surface py/ core actually needs when
// MICROPY_ENABLE_GC=1 (confirmed by grep: malloc/free/realloc are
// macro-redirected to gc_alloc/gc_free/gc_realloc in that config, and
// DEBUG_printf compiles to nothing unless MICROPY_DEBUG_VERBOSE is set).
// Do not add more than this without re-checking against the real call
// sites — this mirrors ports/bare-arm/lib.c's string half exactly, which
// is the precedent MicroPython's own minimal ports use for this.

#include <stddef.h>

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = s1, *b = s2;
    while (n--) {
        int c = *a++ - *b++;
        if (c) {
            return c;
        }
    }
    return 0;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    if (s < d && d < s + n) {
        d += n - 1;
        s += n - 1;
        while (n--) {
            *d-- = *s--;
        }
    } else {
        while (n--) {
            *d++ = *s++;
        }
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t n) {
    return memmove(dest, src, n);
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n > 0 && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strcmp(const char *s1, const char *s2) {
    return strncmp(s1, s2, (size_t)-1);
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) {
        p++;
    }
    return (size_t)(p - s);
}

// abs() is the only stdlib.h symbol py/ core actually calls at runtime
// (confirmed by grep across py/*.c) -- everything else declared in the
// stub stdlib.h/stdio.h headers is dead code behind feature guards
// (#if 0, MICROPY_EMIT_INLINE_THUMB, etc.) that must parse but never
// compiles a body, so it needs no real definition here.
int abs(int n) {
    return n < 0 ? -n : n;
}
