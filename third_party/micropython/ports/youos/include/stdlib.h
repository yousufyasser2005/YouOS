#pragma once
// Bare cross-compiler (no newlib/libc behind it) -- this stub exists only
// so py/ core's #include <stdlib.h> parses. Everything here is declared
// but NOT defined, except abs() (see libc_shim.c): grep across py/*.c
// confirmed abs() is the only symbol from this header actually called at
// runtime. Every other declaration below is reached only through dead
// code behind feature guards (malloc/realloc/free are macro-redirected
// to gc_alloc/gc_free/gc_realloc when MICROPY_ENABLE_GC=1 -- see
// py/malloc.c). If the linker ever reports an undefined reference to one
// of these, that means the dead-code assumption was wrong for that
// symbol -- investigate the call site rather than blindly adding a body.
#include <stddef.h>

void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
int abs(int n);
long labs(long n);
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
int atoi(const char *nptr);
void abort(void);
void exit(int status);
