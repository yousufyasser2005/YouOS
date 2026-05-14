#include <kernel/syscall.h>
#include <kernel/process.h>
#include <kernel/vga.h>

uint64_t kernel_stack_top = 0;
static uint8_t syscall_kernel_stack[16384];

static uint64_t sys_exit(uint64_t code,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)code;(void)a2;(void)a3;(void)a4;(void)a5;
    process_exit(); return 0;
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

typedef uint64_t (*syscall_fn_t)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static syscall_fn_t syscall_table[SYSCALL_COUNT] = {
    sys_exit, sys_write, sys_read, sys_getpid, sys_yield, sys_sleep
};

uint64_t syscall_handler(uint64_t num,uint64_t a1,uint64_t a2,
                          uint64_t a3,uint64_t a4,uint64_t a5)
{
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

void syscall_init(void)
{
    kernel_stack_top=(uint64_t)syscall_kernel_stack+sizeof(syscall_kernel_stack);
    uint64_t efer=rdmsr(0xC0000080);
    efer|=1; wrmsr(0xC0000080,efer);
    /* STAR[63:48]=0x0018: sysretq sets CS=0x18+16=0x28, SS=0x18+8=0x20(user_data) */
    uint64_t star=((uint64_t)0x0008<<32)|((uint64_t)0x0018<<48);
    wrmsr(0xC0000081,star);
    wrmsr(0xC0000082,(uint64_t)syscall_entry);
    wrmsr(0xC0000084,0x200);
}
