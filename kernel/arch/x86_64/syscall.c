#include <kernel/syscall.h>
#include <kernel/process.h>
#include <kernel/vga.h>
#include <kernel/vfs.h>
#include <kernel/kjmp.h>

uint64_t kernel_stack_top  = 0;
uint64_t kernel_return_rsp = 0;
kjmp_buf_t kernel_exit_jmp;
int        kernel_exit_jmp_valid = 0;
static uint8_t syscall_kernel_stack[16384];

static uint64_t sys_exit(uint64_t code,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)code;(void)a2;(void)a3;(void)a4;(void)a5;
    process_current()->state = PROCESS_DEAD;
    if (kernel_exit_jmp_valid) {
        kernel_exit_jmp_valid = 0;
        __asm__ volatile("sti");
        klongjmp(&kernel_exit_jmp);
    }
    return 0;
}

static uint64_t sys_write(uint64_t fd,uint64_t buf,uint64_t len,uint64_t a4,uint64_t a5){
    (void)a4;(void)a5;
    const char* s=(const char*)buf;
    if(!s||len==0) return 0;
    if(fd==1) vga_set_color(VGA_WHITE,VGA_BLACK);
    else if(fd==2) vga_set_color(VGA_LIGHT_RED,VGA_BLACK);
    else return (uint64_t)-1;
    for(uint64_t i=0;i<len;i++) vga_putchar(s[i]);
    vga_set_color(VGA_LIGHT_GREY,VGA_BLACK);
    return len;
}

static uint64_t sys_read(uint64_t fd,uint64_t buf,uint64_t len,uint64_t a4,uint64_t a5){
    (void)a4;(void)a5;
    if(fd!=0) return (uint64_t)-1;
    char* b=(char*)buf; uint64_t i=0;
    extern char keyboard_getchar(void);
    while(i<len){char c=keyboard_getchar();b[i++]=c;if(c=='\n')break;}
    return i;
}

static uint64_t sys_getpid(uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    return process_current()->pid;
}

static uint64_t sys_yield(uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    process_yield(); return 0;
}

static uint64_t sys_sleep(uint64_t t,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a2;(void)a3;(void)a4;(void)a5;
    process_sleep(t); return 0;
}

static uint64_t sys_open(uint64_t path,uint64_t flags,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a3;(void)a4;(void)a5;
    return (uint64_t)vfs_open((const char*)path,(int)flags);
}

static uint64_t sys_close(uint64_t fd,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a2;(void)a3;(void)a4;(void)a5;
    return (uint64_t)vfs_close((int)fd);
}

static uint64_t sys_fread(uint64_t fd,uint64_t buf,uint64_t size,uint64_t a4,uint64_t a5){
    (void)a4;(void)a5;
    return vfs_read((int)fd,(void*)buf,size);
}

typedef uint64_t (*syscall_fn_t)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static syscall_fn_t syscall_table[SYSCALL_COUNT] = {
    sys_exit, sys_write, sys_read, sys_getpid, sys_yield, sys_sleep,
    sys_open, sys_close, sys_fread
};

uint64_t syscall_handler(uint64_t num,uint64_t a1,uint64_t a2,
                         uint64_t a3,uint64_t a4,uint64_t a5){
    if(num>=SYSCALL_COUNT||!syscall_table[num]) return (uint64_t)-1;
    return syscall_table[num](a1,a2,a3,a4,a5);
}

static inline void wrmsr(uint32_t msr,uint64_t val){
    __asm__ volatile("wrmsr"::"c"(msr),"a"((uint32_t)val),"d"((uint32_t)(val>>32)));
}
static inline uint64_t rdmsr(uint32_t msr){
    uint32_t lo,hi;
    __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(msr));
    return ((uint64_t)hi<<32)|lo;
}

extern void syscall_entry(void);
void syscall_init(void){
    kernel_stack_top=(uint64_t)syscall_kernel_stack+sizeof(syscall_kernel_stack);
    uint64_t efer=rdmsr(0xC0000080);
    efer|=1; wrmsr(0xC0000080,efer);
    uint64_t star=((uint64_t)0x0008<<32)|((uint64_t)0x0018<<48);
    wrmsr(0xC0000081,star);
    wrmsr(0xC0000082,(uint64_t)syscall_entry);
    wrmsr(0xC0000084,0x200);
}
