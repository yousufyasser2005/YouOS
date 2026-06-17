#pragma once
#include <stdint.h>

/* Syscall numbers */
#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_READ      2
#define SYS_GETPID    3
#define SYS_YIELD     4
#define SYS_SLEEP     5
#define SYS_OPEN      6
#define SYS_CLOSE     7
#define SYS_FREAD     8
#define SYS_SHUTDOWN  9
#define SYS_REBOOT    10
#define SYS_EXEC      11
#define SYS_FBINFO    12
#define SYS_FBWRITE   13

/* 6-argument syscall wrapper */
static inline uint64_t _sc(uint64_t n, uint64_t a, uint64_t b,
                            uint64_t c, uint64_t d, uint64_t e) {
    uint64_t r;
    register uint64_t r10 __asm__("r10") = d;
    register uint64_t r8  __asm__("r8")  = e;
    __asm__ volatile("syscall"
        : "=a"(r)
        : "0"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory");
    return r;
}

static inline void    sys_exit(int c)
    { _sc(SYS_EXIT, (uint64_t)c, 0, 0, 0, 0); }
static inline int64_t sys_write(int fd, const void* b, uint64_t l)
    { return (int64_t)_sc(SYS_WRITE, (uint64_t)fd, (uint64_t)b, l, 0, 0); }
static inline int64_t sys_read(int fd, void* b, uint64_t l)
    { return (int64_t)_sc(SYS_READ, (uint64_t)fd, (uint64_t)b, l, 0, 0); }
static inline int     sys_open(const char* p, int f)
    { return (int)_sc(SYS_OPEN, (uint64_t)p, (uint64_t)f, 0, 0, 0); }
static inline int     sys_close(int fd)
    { return (int)_sc(SYS_CLOSE, (uint64_t)fd, 0, 0, 0, 0); }
static inline int64_t sys_fread(int fd, void* b, uint64_t l)
    { return (int64_t)_sc(SYS_FREAD, (uint64_t)fd, (uint64_t)b, l, 0, 0); }
static inline int     sys_getpid(void)
    { return (int)_sc(SYS_GETPID, 0, 0, 0, 0, 0); }
static inline void    sys_yield(void)
    { _sc(SYS_YIELD, 0, 0, 0, 0, 0); }
static inline void    sys_sleep(uint64_t t)
    { _sc(SYS_SLEEP, t, 0, 0, 0, 0); }
static inline void    sys_shutdown(void)
    { _sc(SYS_SHUTDOWN, 0, 0, 0, 0, 0); }
static inline void    sys_reboot(void)
    { _sc(SYS_REBOOT, 0, 0, 0, 0, 0); }
static inline int64_t sys_exec(const char* name)
    { return (int64_t)_sc(SYS_EXEC, (uint64_t)name, 0, 0, 0, 0); }
static inline int64_t sys_fbinfo(uint64_t* buf)
    { return (int64_t)_sc(SYS_FBINFO, (uint64_t)buf, 0, 0, 0, 0); }
static inline int64_t sys_fbwrite(uint64_t x, uint64_t y, uint64_t w,
                                   uint64_t h, void* pixels)
    { return (int64_t)_sc(SYS_FBWRITE, x, y, w, h, (uint64_t)pixels); }
#define SYS_KEYPOLL  14
#define SYS_TICKS    15
static inline int64_t sys_keypoll(void)
    { return (int64_t)_sc(SYS_KEYPOLL, 0, 0, 0, 0, 0); }
static inline uint64_t sys_ticks(void)
    { return _sc(SYS_TICKS, 0, 0, 0, 0, 0); }

/* syscall 16 — mouse: out[0]=X  out[1]=Y  out[2]=buttons */
static inline long sys_mouseread(unsigned long long *out) {
    return _sc(16, (long)out, 0, 0, 0, 0);
}

/* syscall 17 — readdir: fills entries[], returns count
 * entry layout: name[32], size(u32), is_dir(u8) = 37 bytes each */
#define DIRENT_SIZE 37
static inline long sys_readdir(void* buf, long max) {
    return (long)_sc(17, (uint64_t)buf, (uint64_t)max, 0, 0, 0);
}

/* syscall 18 — save file: write buf[0..size] to /disk/<filename> */
static inline long sys_save_file(unsigned long long path,
                                  unsigned long long buf,
                                  unsigned long long size){
    return (long)_sc(18,path,buf,size,0,0);
}
/* syscalls 19-21: stat / mkdir / unlink */
#define SYS_STAT   19
#define SYS_MKDIR  20
#define SYS_UNLINK 21
static inline int sys_stat(const char* path, unsigned int* sz, unsigned char* isd) {
    return (int)_sc(SYS_STAT,(uint64_t)path,(uint64_t)sz,(uint64_t)isd,0,0); }
static inline int sys_mkdir(const char* path) {
    return (int)_sc(SYS_MKDIR,(uint64_t)path,0,0,0,0); }
static inline int sys_unlink(const char* path) {
    return (int)_sc(SYS_UNLINK,(uint64_t)path,0,0,0,0); }
/* syscalls 22-24: IPC message queues */
#define SYS_MSGPOST  22
#define SYS_MSGRECV  23
#define SYS_MQCREATE 24
static inline int sys_mqcreate(const char* name){
    return (int)_sc(SYS_MQCREATE,(uint64_t)name,0,0,0,0); }
static inline int sys_msgpost(const char* name,const void* data,unsigned int len){
    return (int)_sc(SYS_MSGPOST,(uint64_t)name,(uint64_t)data,(uint64_t)len,0,0); }
static inline int sys_msgrecv(const char* name,void* data,unsigned int* len,unsigned int* from){
    return (int)_sc(SYS_MSGRECV,(uint64_t)name,(uint64_t)data,(uint64_t)len,(uint64_t)from,0); }
/* syscall 25: rename */
#define SYS_RENAME 25
static inline int sys_rename(const char* old_path, const char* new_path){
    return (int)_sc(SYS_RENAME,(uint64_t)old_path,(uint64_t)new_path,0,0,0); }
/* syscall 26: readdir2 — list subdirectory by path */
#define SYS_READDIR2 26
static inline long sys_readdir2(const char* path, void* buf, long max){
    return (long)_sc(SYS_READDIR2,(uint64_t)path,(uint64_t)buf,(uint64_t)max,0,0); }
