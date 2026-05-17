#pragma once
#include <stdint.h>

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_GETPID  3
#define SYS_YIELD   4
#define SYS_SLEEP   5
#define SYS_OPEN    6
#define SYS_CLOSE   7
#define SYS_FREAD    8
#define SYS_SHUTDOWN 9
#define SYS_REBOOT   10

static inline uint64_t _sc(uint64_t n,uint64_t a,uint64_t b,uint64_t c){
    uint64_t r;
    __asm__ volatile("syscall":"=a"(r):"0"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");
    return r;
}

static inline void    sys_exit(int c)                             { _sc(SYS_EXIT,c,0,0); }
static inline int64_t sys_write(int fd,const void* b,uint64_t l) { return _sc(SYS_WRITE,fd,(uint64_t)b,l); }
static inline int64_t sys_read(int fd,void* b,uint64_t l)        { return _sc(SYS_READ,fd,(uint64_t)b,l); }
static inline int     sys_open(const char* p,int f)              { return _sc(SYS_OPEN,(uint64_t)p,f,0); }
static inline int     sys_close(int fd)                          { return _sc(SYS_CLOSE,fd,0,0); }
static inline int64_t sys_fread(int fd,void* b,uint64_t l)       { return _sc(SYS_FREAD,fd,(uint64_t)b,l); }
static inline void    sys_shutdown(void)                         { _sc(SYS_SHUTDOWN,0,0,0); }
static inline void    sys_reboot(void)                           { _sc(SYS_REBOOT,0,0,0); }

static inline uint64_t ustrlen(const char* s){uint64_t n=0;while(s[n])n++;return n;}
static inline void print(const char* s){sys_write(1,s,ustrlen(s));}
static inline void println(const char* s){print(s);print("\n");}
static inline int ustrcmp(const char* a,const char* b){
    while(*a&&*b&&*a==*b){a++;b++;}return *a-*b;
}
static inline int ustrncmp(const char* a,const char* b,uint64_t n){
    while(n&&*a&&*b&&*a==*b){a++;b++;n--;}return n?(*a-*b):0;
}
