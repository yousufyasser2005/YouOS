/*
 * YouOS - System Call Handler
 * syscall.c
 */

#include <kernel/syscall.h>
#include <kernel/process.h>
#include <kernel/vga.h>

/* Kernel stack top — used by syscall_entry.asm */
uint64_t kernel_stack_top = 0;

/* =========================================================================
 * Individual syscall implementations
 * ========================================================================= */

/* sys_exit — terminate current process */
static uint64_t sys_exit(uint64_t code, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    (void)code;
    process_exit();
    return 0;
}

/* sys_write — write to a file descriptor
 * fd 1 = stdout (VGA), fd 2 = stderr (VGA red)
 */
static uint64_t sys_write(uint64_t fd, uint64_t buf_addr, uint64_t len,
                           uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    const char* buf = (const char*)buf_addr;
    if (!buf || len == 0) return 0;

    if (fd == 1) {
        /* stdout — white */
        vga_set_color(VGA_WHITE, VGA_BLACK);
    } else if (fd == 2) {
        /* stderr — red */
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    } else {
        return (uint64_t)-1;   /* bad fd */
    }

    for (uint64_t i = 0; i < len; i++) vga_putchar(buf[i]);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    return len;
}

/* sys_read — read from fd (keyboard only for now) */
static uint64_t sys_read(uint64_t fd, uint64_t buf_addr, uint64_t len,
                          uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    if (fd != 0) return (uint64_t)-1;   /* only stdin */

    char* buf = (char*)buf_addr;
    uint64_t i = 0;
    extern char keyboard_getchar(void);
    while (i < len) {
        char c = keyboard_getchar();
        buf[i++] = c;
        if (c == '\n') break;
    }
    return i;
}

/* sys_getpid — return current PID */
static uint64_t sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3,
                             uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return process_current()->pid;
}

/* sys_yield — voluntarily give up CPU */
static uint64_t sys_yield(uint64_t a1, uint64_t a2, uint64_t a3,
                           uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    process_yield();
    return 0;
}

/* sys_sleep — sleep for n ticks */
static uint64_t sys_sleep(uint64_t ticks, uint64_t a2, uint64_t a3,
                           uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    process_sleep(ticks);
    return 0;
}

/* sys_sbrk — grow/shrink heap (stub for now) */
static uint64_t sys_sbrk(uint64_t increment, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5)
{
    (void)increment; (void)a2; (void)a3; (void)a4; (void)a5;
    return 0;   /* stub */
}

/* =========================================================================
 * Syscall dispatch table
 * ========================================================================= */
typedef uint64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t);

static syscall_fn_t syscall_table[SYSCALL_COUNT] = {
    [SYS_EXIT]   = sys_exit,
    [SYS_WRITE]  = sys_write,
    [SYS_READ]   = sys_read,
    [SYS_GETPID] = sys_getpid,
    [SYS_YIELD]  = sys_yield,
    [SYS_SLEEP]  = sys_sleep,
    [SYS_SBRK]   = sys_sbrk,
};

/* =========================================================================
 * syscall_handler — called by syscall_entry.asm
 * ========================================================================= */
uint64_t syscall_handler(uint64_t syscall_no,
                          uint64_t arg1, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5)
{
    if (syscall_no >= SYSCALL_COUNT || !syscall_table[syscall_no]) {
        vga_puts_color("  [!!] Unknown syscall: ", VGA_LIGHT_RED, VGA_BLACK);
        return (uint64_t)-1;
    }
    return syscall_table[syscall_no](arg1, arg2, arg3, arg4, arg5);
}

/* =========================================================================
 * syscall_init — set up SYSCALL/SYSRET MSRs
 *
 * MSRs we need:
 *   IA32_EFER  (0xC0000080) — enable SCE (syscall extensions) bit
 *   IA32_STAR  (0xC0000081) — segment selectors for syscall/sysret
 *   IA32_LSTAR (0xC0000082) — 64-bit syscall entry point (RIP)
 *   IA32_FMASK (0xC0000084) — RFLAGS mask (clear IF on syscall)
 * ========================================================================= */
static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = val & 0xFFFFFFFF;
    uint32_t hi = val >> 32;
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

extern void syscall_entry(void);

/* Kernel stack for syscall handling */
static uint8_t syscall_kernel_stack[16384];  /* 16KB */

void syscall_init(void)
{
    /* Set kernel stack top */
    kernel_stack_top = (uint64_t)syscall_kernel_stack + sizeof(syscall_kernel_stack);

    /* Enable SCE bit in EFER */
    uint64_t efer = rdmsr(0xC0000080);
    efer |= (1 << 0);   /* SCE = System Call Extensions */
    wrmsr(0xC0000080, efer);

    /*
     * STAR MSR layout:
     *   Bits 63:48 = SYSRET CS  (user code = this + 16, user data = this + 8)
     *   Bits 47:32 = SYSCALL CS (kernel code segment selector)
     *
     * Our GDT:
     *   0x00 = null
     *   0x08 = kernel code  ← SYSCALL CS
     *   0x10 = kernel data
     *   0x18 = user code    ← SYSRET CS = 0x18 (but CPU adds 16 for code)
     *   0x20 = user data
     *
     * For SYSRET: CPU loads CS from STAR[63:48]+16, SS from STAR[63:48]+8
     * So STAR[63:48] should be 0x10 (user data - 8 = 0x18-8 = 0x10)
     * Actually: STAR[63:48] = 0x0013 means SS=0x13|3=0x1B, CS=0x1B+... 
     * Simpler: use 0x0008 for kernel CS in [47:32], 0x0010 for user in [63:48]
     */
    uint64_t star = 0;
    star |= ((uint64_t)0x0008 << 32);  /* kernel CS for SYSCALL */
    star |= ((uint64_t)0x0010 << 48);  /* base for SYSRET selectors */
    wrmsr(0xC0000081, star);

    /* LSTAR — syscall entry point */
    wrmsr(0xC0000082, (uint64_t)syscall_entry);

    /* FMASK — mask RFLAGS on syscall entry (clear IF = disable interrupts) */
    wrmsr(0xC0000084, 0x200);   /* clear IF bit */
}
