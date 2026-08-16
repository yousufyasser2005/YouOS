#pragma once
// See stdlib.h in this same directory for why this stub exists.
// memcpy/memmove/memset/memcmp/strlen/strcmp/strncmp/strchr are DEFINED
// in libc_shim.c (sandbox-tested against real libc under ASan/UBSan,
// 20000+ randomized cases + edge cases, all passing). Everything else
// below is declared only, for dead code that must parse but never links.
#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);

char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
void *memchr(const void *s, int c, size_t n);
char *strstr(const char *haystack, const char *needle);
