#pragma once
// See stdlib.h in this same directory for why this stub exists. Every
// symbol here is declared only, never defined -- every real call site
// found by grep (py/lexer.c's mp_lexer_show_token, py/asmarm.c's debug
// prints, etc.) is behind a feature guard or "#if 0" that's off for this
// build, so these declarations only need to make the header parse, not
// actually link. If the linker reports an undefined reference to one of
// these, paste it back rather than assuming -- that means a call site
// isn't as dead as this analysis found.
#include <stdarg.h>
#include <stddef.h>

typedef struct FILE FILE;
extern FILE *stdin, *stdout, *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
