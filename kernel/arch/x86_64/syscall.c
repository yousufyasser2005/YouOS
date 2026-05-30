#include <kernel/syscall.h>
#include <kernel/process.h>
#include <kernel/vga.h>
#include <kernel/vfs.h>
#include <kernel/elf.h>
#include <kernel/initrd.h>
#include <kernel/kjmp.h>
#include <kernel/gdt.h>
#include <kernel/keyboard.h>
#include <kernel/fb.h>

uint64_t kernel_stack_top  = 0;
uint64_t kernel_return_rsp = 0;
kjmp_buf_t kernel_exit_jmp;
int        kernel_exit_jmp_valid = 0;
static uint8_t child_kstack[32768];
static uint8_t syscall_kernel_stack[262144];

static uint64_t   exec_saved_kstack;
static uint64_t   exec_saved_user_rsp;
static uint64_t   exec_saved_cr3;
static kjmp_buf_t exec_saved_jmp;

static uint64_t sys_exit(uint64_t code,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)code;(void)a2;(void)a3;(void)a4;(void)a5;
    process_current()->state = PROCESS_DEAD;
    if (kernel_exit_jmp_valid) {
        kernel_exit_jmp_valid = 0;
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

static uint64_t sys_exec(uint64_t path, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2;(void)a3;(void)a4;(void)a5;
    const char* name = (const char*)path;
    uint64_t elf_size = 0;
    const void* elf_data = initrd_find(name, &elf_size);
    if (!elf_data) return (uint64_t)-1;

    elf_load_result_t res;
    address_space_t proc_as = vmm_create_user_as();
    if (elf_load(&proc_as, elf_data, elf_size, &res) != 0) return (uint64_t)-2;

    extern uint64_t pmm_alloc_pages(uint64_t);
    uint64_t stack_base = pmm_alloc_pages(4);
    uint64_t stack_top  = stack_base + 4 * 4096;
    for (uint64_t a = stack_base; a < stack_top; a += 4096)
        vmm_map(&proc_as, a, a, 0x7);

    extern uint64_t user_rsp_tmp;
    exec_saved_kstack    = kernel_stack_top;
    exec_saved_user_rsp  = user_rsp_tmp;
    exec_saved_jmp       = kernel_exit_jmp;
    __asm__ volatile("mov %%cr3, %0" : "=r"(exec_saved_cr3));

    kernel_exit_jmp_valid = 1;
    int exited = ksetjmp(&kernel_exit_jmp);
    if (!exited) {
        kernel_stack_top = (uint64_t)child_kstack + sizeof(child_kstack);
        tss_set_kernel_stack(kernel_stack_top);
        vmm_switch(&proc_as);
        __asm__ volatile("mov %%cr3,%%rax;mov %%rax,%%cr3":::"rax","memory");
        extern void jump_to_userspace(uint64_t, uint64_t);
        jump_to_userspace(res.entry, stack_top);
    }

    /* Child exited — restore parent shell context */
    kernel_exit_jmp      = exec_saved_jmp;
    kernel_exit_jmp_valid = 1;
    kernel_stack_top     = exec_saved_kstack;
    tss_set_kernel_stack(kernel_stack_top);
    user_rsp_tmp         = exec_saved_user_rsp;

    __asm__ volatile("mov %0, %%cr3" :: "r"(exec_saved_cr3) : "memory");
    return 0;
}

static uint64_t sys_shutdown(uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    __asm__ volatile("outw %0, %1"::"a"((uint16_t)0x2000),"Nd"((uint16_t)0x604));
    __asm__ volatile("outw %0, %1"::"a"((uint16_t)0x2000),"Nd"((uint16_t)0xB004));
    __asm__ volatile("cli;hlt");
    return 0;
}

static uint64_t sys_reboot(uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    uint8_t tmp;
    do { __asm__ volatile("inb $0x64,%0":"=a"(tmp)); } while(tmp & 0x02);
    __asm__ volatile("outb %0,$0x64"::"a"((uint8_t)0xFE));
    __asm__ volatile("cli;hlt");
    return 0;
}

static uint64_t sys_fread(uint64_t fd,uint64_t buf,uint64_t size,uint64_t a4,uint64_t a5){
    (void)a4;(void)a5;
    return vfs_read((int)fd,(void*)buf,size);
}

/* ── Framebuffer syscalls ────────────────────────────────────── */

/* sys_fbinfo(buf) — writes 5 uint64_t values into user buffer:
 *   [0] framebuffer physical address
 *   [1] width  (pixels)
 *   [2] height (pixels)
 *   [3] pitch  (bytes per row)
 *   [4] bpp    (bits per pixel)
 * Returns 0 on success, -1 if no framebuffer */
static uint64_t sys_fbinfo(uint64_t buf, uint64_t a2, uint64_t a3,
                            uint64_t a4, uint64_t a5) {
    (void)a2;(void)a3;(void)a4;(void)a5;
    if (!fb_available()) return (uint64_t)-1;
    uint64_t* out = (uint64_t*)buf;
    fb_info_t* info = fb_get_info();
    out[0] = info->addr;
    out[1] = info->width;
    out[2] = info->height;
    out[3] = info->pitch;
    out[4] = info->bpp;
    return 0;
}

/* sys_fbwrite(x, y, w, h, pixels) — blit a w×h ARGB pixel buffer
 * at screen position (x,y). pixels points to w*h uint32_t values.
 * Returns 0 on success, -1 if no framebuffer or out of bounds. */
static uint64_t sys_fbwrite(uint64_t x, uint64_t y, uint64_t w,
                             uint64_t h, uint64_t pixels) {
    if (!fb_available()) return (uint64_t)-1;
    fb_info_t* info = fb_get_info();
    if (x + w > info->width || y + h > info->height) return (uint64_t)-1;
    uint32_t* src = (uint32_t*)pixels;
    for (uint64_t row = 0; row < h; row++) {
        uint32_t* dst = (uint32_t*)(info->addr
                        + (y + row) * info->pitch
                        + x * (info->bpp / 8));
        for (uint64_t col = 0; col < w; col++)
            dst[col] = src[row * w + col];
    }
    return 0;
}

static uint64_t sys_keypoll(uint64_t a1,uint64_t a2,uint64_t a3,
                             uint64_t a4,uint64_t a5) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    extern int keyboard_get_event(key_event_t*);
    extern int keyboard_available(void);
    if (!keyboard_available()) return 0;
    key_event_t e;
    if (keyboard_get_event(&e) && e.ascii) return (uint64_t)e.ascii;
    return 0;
}

static uint64_t sys_ticks(uint64_t a1,uint64_t a2,uint64_t a3,
                           uint64_t a4,uint64_t a5) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    extern uint64_t scheduler_get_ticks(void);
    return scheduler_get_ticks();
}

typedef uint64_t (*syscall_fn_t)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static syscall_fn_t syscall_table[SYSCALL_COUNT] = {
    sys_exit, sys_write, sys_read, sys_getpid, sys_yield, sys_sleep,
    sys_open, sys_close, sys_fread,
    sys_shutdown, sys_reboot,
    sys_exec,
    sys_fbinfo, sys_fbwrite,
    sys_keypoll, sys_ticks
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
