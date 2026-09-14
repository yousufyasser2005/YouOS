#include <kernel/syscall.h>
#include <kernel/session.h>
#include <kernel/ipc.h>
#include <kernel/syslog.h>
#include <kernel/crash.h>
#include <kernel/process.h>
#include <kernel/vga.h>
#include <kernel/vfs.h>
#include <kernel/elf.h>
#include <kernel/initrd.h>
#include <kernel/gdt.h>
#include <kernel/keyboard.h>
#include <kernel/fb.h>
#include <kernel/mouse.h>
uint64_t kernel_stack_top  = 0;
uint64_t kernel_return_rsp = 0;
static uint8_t syscall_kernel_stack[262144];

static int path_is_ycfs(const char* p);

static uint64_t sys_exit(uint64_t code,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)code;(void)a2;(void)a3;(void)a4;(void)a5;
    process_t* p = process_current();
    p->state = PROCESS_DEAD;
    /* Every real process is now spawned via process_create() (real_exit
     * is set unconditionally there), including the fallback shell's
     * exec command -- the last holdout. The old kernel_exit_jmp_valid
     * longjmp fallback below is therefore permanently unreachable and
     * has been removed; see the handoff doc for the verification that
     * made this safe. process_exit() marks DEAD (already done above,
     * but it does so again harmlessly) and never returns. */
    process_exit();
    return 0;
}
/* Builds "term_out_<pid>" or "term_in_<pid>" into `out` (caller-
 * provided, must be at least 21 bytes: 9-char longest prefix +
 * up to 10 digits for a uint32_t pid + nul). Phase 2 of true
 * concurrent multi-program execution: the naming convention a
 * windowed process's fd 0/1/2 get redirected through, via the
 * existing ipc_post()/ipc_recv() primitive -- and the same convention
 * a future window manager (Phase 3) uses directly (via sys_msgpost()/
 * sys_msgrecv()) to drain a window's output and feed it input,
 * without needing any new syscall of its own to look the names up. */
static void term_queue_name(uint32_t pid, int is_out, char* out) {
    const char* prefix = is_out ? "term_out_" : "term_in_";
    int oi = 0;
    while (prefix[oi]) { out[oi] = prefix[oi]; oi++; }
    if (pid == 0) { out[oi++] = '0'; }
    else {
        char tmp[12]; int ti = 0;
        uint32_t pv = pid;
        while (pv) { tmp[ti++] = (char)('0' + (pv % 10)); pv /= 10; }
        while (ti > 0) out[oi++] = tmp[--ti];
    }
    out[oi] = 0;
}

static uint64_t sys_write(uint64_t fd,uint64_t buf,uint64_t len,uint64_t a4,uint64_t a5){
    (void)a4;(void)a5;
    const char* s=(const char*)buf;
    if(!s||len==0) return 0;
    if(fd!=1 && fd!=2) return (uint64_t)-1;

    if (process_current()->windowed) {
        /* Phase 2: redirect through this process's own output queue
         * instead of the single shared console, chunked to
         * IPC_MAX_MSGLEN per message since ipc_post() deals in whole
         * messages, not a byte stream. If the queue fills up (a slow
         * reader not draining it -- 32 messages deep) the remainder is
         * dropped rather than blocking: sys_write() has never blocked
         * before, and introducing that now would be a bigger behavior
         * change than this redirection is meant to make. A caller that
         * cares can check the returned count against `len`. */
        char qname[24];
        term_queue_name(process_current()->pid, 1, qname);
        uint64_t sent = 0;
        while (sent < len) {
            uint32_t chunk = (uint32_t)(len - sent);
            if (chunk > IPC_MAX_MSGLEN) chunk = IPC_MAX_MSGLEN;
            if (ipc_post(qname, s + sent, chunk) != 0) break;
            sent += chunk;
        }
        return sent;
    }

    if(fd==1) vga_set_color(VGA_WHITE,VGA_BLACK);
    else vga_set_color(VGA_LIGHT_RED,VGA_BLACK);
    for(uint64_t i=0;i<len;i++) vga_putchar(s[i]);
    vga_set_color(VGA_LIGHT_GREY,VGA_BLACK);
    return len;
}
static uint64_t sys_read(uint64_t fd,uint64_t buf,uint64_t len,uint64_t a4,uint64_t a5){
    (void)a4;(void)a5;
    if(fd!=0) return (uint64_t)-1;
    char* b=(char*)buf; uint64_t i=0;

    if (process_current()->windowed) {
        /* Phase 2: same blocking-via-polling shape keyboard_getchar()
         * already uses below (yield and retry until something's
         * there), just sourced from this process's own input queue
         * instead of the shared keyboard stream. Each message is
         * expected to be exactly one character -- the convention
         * Phase 3's window manager posts keystrokes under. */
        char qname[24];
        term_queue_name(process_current()->pid, 0, qname);
        while (i < len) {
            uint8_t c; uint32_t rlen = 0, from = 0;
            if (ipc_recv(qname, &c, &rlen, &from) != 0) { process_yield(); continue; }
            if (rlen < 1) continue;
            b[i++] = (char)c;
            if ((char)c == '\n') break;
        }
        return i;
    }

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
/* Shared by sys_exec() (blocking) and sys_spawn() (non-blocking, Phase 1
 * of the true-concurrency work): builds a fresh child process from an
 * initrd ELF, ready to run, but does NOT wait on or reap it -- that part
 * differs between the two callers. Returns the new process_t* on
 * success, or NULL with *err set to sys_exec()'s existing error codes
 * (-1 not found, -2 elf_load failed, -4 process_create failed), so both
 * callers can keep returning the exact values they always have.
 *
 * `path`/`arg` are raw pointers into the CALLING process's own address
 * space -- copied out before the CR3 switch below, for the same reason
 * sys_exec() always has (see the CR3 comment further down). */
static process_t* spawn_common(const char* path, const char* arg, uint64_t* err) {
    const char* name = path;
    syslog_write("EXEC", name);

    uint64_t elf_size = 0;
    const void* elf_data = initrd_find(name, &elf_size);
    if (!elf_data) { *err = (uint64_t)-1; return 0; }

    /* Copy the program name AND the optional argument string (e.g.
     * yourun's script path) out of the CALLER's own address space NOW,
     * while CR3 is still whatever the caller had active -- syscall
     * entry doesn't change CR3 (see syscall_entry.asm), so this is the
     * only point at which `name`/`arg` (pointers into the caller's own
     * memory) are guaranteed dereferenceable. Once we switch to
     * kernel_as below for page-table construction, they would not be
     * -- and process_create() dereferences its own `name` argument
     * internally (copying it into process_t.name), which would happen
     * AFTER that switch if we passed the raw pointer through instead
     * of this local copy. */
    char name_buf[PROCESS_NAME_MAX];
    {
        int ni = 0;
        while (name[ni] && ni < PROCESS_NAME_MAX - 1) { name_buf[ni] = name[ni]; ni++; }
        name_buf[ni] = 0;
    }
    char arg_buf[PROCESS_EXEC_ARG_SIZE];
    {
        int ai = 0;
        if (arg) {
            while (arg[ai] && ai < PROCESS_EXEC_ARG_SIZE - 1) {
                arg_buf[ai] = arg[ai];
                ai++;
            }
        }
        arg_buf[ai] = 0;
    }

    /* Page-table construction (vmm_create_user_as/elf_load/stack mapping)
     * dereferences physical addresses as if they were directly-mapped
     * pointers, which only holds under the kernel's own pristine identity
     * map. A user process's own CR3 (e.g. the caller's) can have that low
     * 1GB region partially punched by its own ELF load at 0x400000, so we
     * must do all of this construction work under kernel_as, not whatever
     * CR3 happened to be active when this was called. Unlike the old
     * implementation, there is no manual "restore the caller's CR3"
     * needed anywhere below: do_switch() (via each caller's own wait/yield
     * loop) sets CR3 unconditionally based on process_t.as, comparing
     * LOGICAL address-space values rather than reading the actual
     * register, so it doesn't matter what the register's prior value was
     * by the time it runs. Error-return paths before that point DO need
     * to restore it manually, since we're about to sysretq straight back
     * into the caller's own ring-3 code otherwise. */
    extern address_space_t kernel_as;
    __asm__ volatile("mov %0, %%cr3" :: "r"(kernel_as.pml4_phys) : "memory");

    elf_load_result_t res;
    address_space_t proc_as = vmm_create_user_as();
    if (elf_load(&proc_as, elf_data, elf_size, &res) != 0) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(process_current()->as.pml4_phys) : "memory");
        *err = (uint64_t)-2;
        return 0;
    }

    extern uint64_t pmm_alloc_pages(uint64_t);
    uint64_t stack_base = pmm_alloc_pages(16);
    uint64_t stack_top  = stack_base + 16 * 4096;
    for (uint64_t a = stack_base; a < stack_top; a += 4096)
        /* 0x7 = PRESENT|WRITABLE|USER; user-mode stack, writable,
         * never executable. */
        vmm_map(&proc_as, a, a, 0x7 | (nx_supported ? PTE_NO_EXEC : 0));

    process_t* child = process_create(name_buf, process_ring3_trampoline, proc_as);
    if (!child) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(process_current()->as.pml4_phys) : "memory");
        *err = (uint64_t)-4;
        return 0;
    }

    child->user_entry     = res.entry;
    child->user_stack_top = stack_top;
    {
        int ai = 0;
        while (arg_buf[ai] && ai < PROCESS_EXEC_ARG_SIZE - 1) { child->exec_arg[ai] = arg_buf[ai]; ai++; }
        child->exec_arg[ai] = 0;
    }

    return child;
}

static uint64_t sys_exec(uint64_t path, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3;(void)a4;(void)a5;
    uint64_t err = 0;
    process_t* child = spawn_common((const char*)path, (const char*)a2, &err);
    if (!child) return err;

    /* Full-screen clear before handing off — the caller (e.g. desktop)
     * may have left graphical content on screen, and the exec'd
     * process's own text console only draws into a small fixed region,
     * so without this the old frame stays visible underneath. Safe to
     * do here (still under kernel_as, before the child ever actually
     * runs) since fb_fill/fb_terminal_init are kernel-side, not
     * address-space-dependent. */
    fb_fill(FB_BLACK);
    fb_terminal_init();

    /* Block until the child exits — preserves sys_exec()'s existing
     * synchronous, blocking semantics from the caller's perspective
     * exactly, just via a real scheduler wait instead of a longjmp.
     * do_switch() sets CR3 correctly the moment it picks the child as
     * next, regardless of what the register currently holds. */
    process_wait(child);
    process_reap(child);

    return 0;
}

/* Non-blocking counterpart to sys_exec() -- Phase 1 of true concurrent
 * multi-program execution. Builds and starts the child exactly the same
 * way, but returns its pid immediately instead of waiting for it to
 * exit, and deliberately skips the fb_fill/fb_terminal_init() full-
 * screen clear sys_exec() does: a spawned-but-not-waited-on child isn't
 * meant to take over the caller's whole screen the way a blocking exec
 * does. The caller is responsible for later calling sys_wait_nonblock()
 * on the returned pid -- otherwise the child is never reaped once it
 * exits (the same resource-leak risk sys_exec()'s own process_reap()
 * call exists to avoid). */
static uint64_t sys_spawn(uint64_t path, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a4;(void)a5;
    uint64_t err = 0;
    process_t* child = spawn_common((const char*)path, (const char*)a2, &err);
    if (!child) return err;

    /* Phase 2: a3 nonzero means the caller wants this child's fd 0/1/2
     * redirected through its own IPC queues (see term_queue_name())
     * instead of the single shared console -- the prerequisite for a
     * window manager to host it in its own window rather than it
     * fighting every other process over one shared text console.
     * ipc_post()/ipc_recv() auto-create a queue on first use, so
     * nothing needs to be explicitly created here; whichever side
     * (this child writing output, or its window posting input) touches
     * a queue first brings it into existence. Note: ipc.c's queues are
     * a fixed-size, never-freed registry (IPC_MAX_QUEUES=16, no
     * destroy call exists) -- each windowed process permanently
     * consumes 2 slots for its lifetime and beyond, a real but
     * accepted constraint of the existing, already-tested IPC
     * primitive this reuses rather than something new here. */
    child->windowed = (a3 != 0) ? 1 : 0;

    /* CRITICAL, unlike sys_exec(): spawn_common()'s success path leaves
     * CR3 pointing at kernel_as (needed for the page-table construction
     * inside it) and never restores it -- sys_exec() gets away with
     * this because process_wait() right after it always triggers
     * do_switch(), which restores CR3 correctly as a side effect before
     * ever returning to ring 3. sys_spawn() deliberately has no such
     * call (that's the whole point of not blocking), so nothing else
     * will fix this up -- without restoring it explicitly here, we'd
     * sysretq straight back into the CALLING process's ring-3 code
     * with kernel_as still active, which doesn't have that process's
     * own ELF/stack mapped at all and crashes essentially immediately. */
    __asm__ volatile("mov %0, %%cr3" :: "r"(process_current()->as.pml4_phys) : "memory");

    return child->pid;
}

/* Non-blocking check on a pid previously returned by sys_spawn(). Never
 * blocks or yields, unlike process_wait(): if the process doesn't exist
 * (bad pid, or already reaped by an earlier call), returns -1. If it's
 * still running, returns -2 without touching it further -- the caller
 * is expected to poll again later (e.g. once per desktop frame). If
 * it's exited, reaps it immediately (same as sys_exec()'s own
 * process_reap() call) and returns 0. A caller that never calls this
 * on a pid it spawned leaks that child's resources forever, the same
 * way skipping process_reap() always has. */
static uint64_t sys_wait_nonblock(uint64_t pid, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2;(void)a3;(void)a4;(void)a5;
    process_t* p = process_get((uint32_t)pid);
    if (!p) return (uint64_t)-1;
    if (p->state != PROCESS_DEAD) return (uint64_t)-2;
    process_reap(p);
    return 0;
}

static uint64_t sys_get_exec_arg(uint64_t buf, uint64_t bufsize, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3;(void)a4;(void)a5;
    char* out = (char*)buf;
    if (bufsize == 0) {
        return 0;
    }
    /* exec_arg now lives directly on the calling process's own
     * process_t (set by sys_exec() when it spawned this process),
     * instead of a level-indexed static array. Empty for anything not
     * spawned via sys_exec (e.g. the very first process kernel_main.c
     * launches directly) -- no argument, not an error, since
     * process_t.exec_arg is zero-initialized by kzalloc() in
     * process_create() and scheduler_init(). */
    const char* src = process_current()->exec_arg;
    uint64_t n = 0;
    while (src[n] && n < bufsize - 1) {
        out[n] = src[n];
        n++;
    }
    out[n] = 0;
    return n;
}
static uint64_t sys_shutdown(uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    syslog_write("SHUTDOWN","Clean shutdown");
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
    if (!keyboard_get_event(&e)) return 0;
    if (!e.pressed) return 0;
    /* Scancodes checked FIRST — arrow keys produce numpad ASCII chars
       (4/6/8/2) on some keyboards so we must intercept before e.ascii */
    switch (e.scancode) {
        case 0x48: return 1001;
        case 0x50: return 1002;
        case 0x4B: return 1003;
        case 0x4D: return 1004;
        case 0x47: return 1005;
        case 0x4F: return 1006;
        case 0x53: return 1007;
        case 0x49: return 1008;
        case 0x51: return 1009;
    }
    if (e.ascii) return (uint64_t)e.ascii;
    return 0;
}
static uint64_t sys_ticks(uint64_t a1,uint64_t a2,uint64_t a3,
                           uint64_t a4,uint64_t a5) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    extern uint64_t scheduler_get_ticks(void);
    return scheduler_get_ticks();
}
static uint64_t sys_mouseread(uint64_t buf, uint64_t a2, uint64_t a3,
                               uint64_t a4, uint64_t a5) {
    (void)a2;(void)a3;(void)a4;(void)a5;
    uint64_t* out = (uint64_t*)buf;
    if (!out) return (uint64_t)-1;
    out[0] = (uint64_t)mouse_get_x();
    out[1] = (uint64_t)mouse_get_y();
    out[2] = (uint64_t)mouse_get_buttons();
    return 0;
}
#include <kernel/fat16.h>
static uint64_t sys_readdir(uint64_t buf, uint64_t max, uint64_t a3,
                             uint64_t a4, uint64_t a5) {
    (void)a3;(void)a4;(void)a5;
    fat16_entry_t* entries = (fat16_entry_t*)buf;
    if (!entries || max == 0) return 0;
    return (uint64_t)fat16_list(entries, (int)max);
}
static uint64_t sys_savefile(uint64_t path_arg, uint64_t buf,
                              uint64_t size, uint64_t a4, uint64_t a5) {
    (void)a4;(void)a5;
    const char* path = (const char*)path_arg;
    if (path_is_ycfs(path)) {
        extern int64_t ycfs_savefile(const char*, const void*, uint32_t);
        int64_t n = ycfs_savefile(path, (const void*)buf, (uint32_t)size);
        return (n < 0) ? (uint64_t)-1 : (uint64_t)n;
    }
    /* extract filename after last '/' */
    const char* name = path;
    for(const char* p = path; *p; p++) if(*p=='/') name=p+1;
    if(!name[0]) return (uint64_t)-1;
    syslog_write("SAVEFILE",name);
    int fd = fat16_create(name);
    if(fd < 0) return (uint64_t)-1;
    int n = fat16_write(fd, (const void*)buf, (uint32_t)size);
    fat16_close(fd);
    return (n < 0) ? (uint64_t)-1 : (uint64_t)n;
}
static uint64_t sys_set_session_uid(uint64_t uid, uint64_t gid, uint64_t a3,
                                     uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    session_set_uid((uint32_t)uid, (uint32_t)gid);
    return 0;
}
static uint64_t sys_youdo(uint64_t on, uint64_t a2, uint64_t a3,
                           uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    session_set_elevated((int)on);
    return 0;
}
static uint64_t sys_chmod(uint64_t path, uint64_t perm, uint64_t a3,
                           uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    extern int ycfs_chmod(const char*, uint16_t);
    return (uint64_t)(int64_t)ycfs_chmod((const char*)path, (uint16_t)perm);
}
static uint64_t sys_chown(uint64_t path, uint64_t uid, uint64_t gid,
                           uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    extern int ycfs_chown(const char*, uint32_t, uint32_t);
    return (uint64_t)(int64_t)ycfs_chown((const char*)path, (uint32_t)uid, (uint32_t)gid);
}
static uint64_t sys_fileinfo(uint64_t path, uint64_t uid_out, uint64_t gid_out,
                              uint64_t perm_out, uint64_t a5) {
    (void)a5;
    extern int ycfs_get_owner(const char*, uint32_t*, uint32_t*, uint16_t*);
    return (uint64_t)(int64_t)ycfs_get_owner((const char*)path, (uint32_t*)uid_out,
                                              (uint32_t*)gid_out, (uint16_t*)perm_out);
}
static uint64_t sys_play_pcm(uint64_t ptr, uint64_t count, uint64_t rate,
                              uint64_t channels, uint64_t a5) {
    (void)a5;
    extern int ac97_play_pcm(const int16_t*, uint32_t, uint32_t, uint8_t);
    return (uint64_t)(int64_t)ac97_play_pcm((const int16_t*)ptr, (uint32_t)count,
                                             (uint32_t)rate, (uint8_t)channels);
}
static uint64_t sys_pcm_done(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    extern int ac97_is_done(void);
    return (uint64_t)ac97_is_done();
}
static uint64_t sys_pcm_can_submit(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    extern int ac97_can_submit(void);
    return (uint64_t)ac97_can_submit();
}
static uint64_t sys_play_stream(uint64_t ptr, uint64_t count, uint64_t rate,
                                 uint64_t channels, uint64_t a5) {
    (void)a5;
    extern int ac97_stream_start(const int16_t*, uint32_t, uint32_t, uint8_t);
    return (uint64_t)(int64_t)ac97_stream_start((const int16_t*)ptr, (uint32_t)count,
                                                 (uint32_t)rate, (uint8_t)channels);
}
static uint64_t sys_stream_active(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    extern int ac97_stream_is_playing(void);
    return (uint64_t)ac97_stream_is_playing();
}
static uint64_t sys_ac97_debug(uint64_t which, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    extern uint32_t ac97_debug_irq_fire_count(void);
    extern uint32_t ac97_debug_irq_bcis_count(void);
    extern uint32_t ac97_debug_last_sr(void);
    extern uint32_t ac97_debug_current_civ_lvi(void);
    extern uint32_t ac97_debug_ring_counts(void);
    extern uint32_t ac97_debug_path_counts(void);
    extern void ac97_debug_restart_log_reset(void);
    extern uint32_t ac97_debug_restart_log_get(uint32_t);
    extern uint32_t ac97_debug_cold_start_duration(void);
    extern uint32_t ac97_debug_last_alloc_fail_pages(void);
    extern uint32_t ac97_debug_feed_counts_a(void);
    extern uint32_t ac97_debug_feed_counts_b(void);
    extern uint32_t ac97_stream_played_samples(void);
    if (which == 12) return ac97_stream_played_samples();
    if (which == 0) return ac97_debug_irq_fire_count();
    if (which == 1) return ac97_debug_irq_bcis_count();
    if (which == 2) return ac97_debug_last_sr();
    if (which == 3) return ac97_debug_current_civ_lvi();
    if (which == 4) return ac97_debug_ring_counts();
    if (which == 5) return ac97_debug_path_counts();
    if (which == 6) { ac97_debug_restart_log_reset(); return 0; }
    if (which == 7) return ac97_debug_restart_log_get((uint32_t)a2);
    if (which == 8) return ac97_debug_cold_start_duration();
    if (which == 9) return ac97_debug_last_alloc_fail_pages();
    if (which == 10) return ac97_debug_feed_counts_a();
    return ac97_debug_feed_counts_b(); /* which == 11 */
}
typedef uint64_t (*syscall_fn_t)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);

static uint64_t sys_readcrash(uint64_t buf,uint64_t sz,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a3;(void)a4;(void)a5;
    return (uint64_t)(int64_t)crash_read((void*)buf,(uint32_t)sz);
}
static uint64_t sys_mousewheel(uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    return (uint64_t)(int64_t)mouse_get_wheel_delta();
}
static uint64_t sys_mousedbg(uint64_t buf,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a2;(void)a3;(void)a4;(void)a5;
    uint64_t* out=(uint64_t*)buf;
    if(!out)return (uint64_t)-1;
    out[0]=(uint64_t)mouse_get_debug_len();
    out[1]=(uint64_t)mouse_get_debug_byte3();
    out[2]=(uint64_t)mouse_has_wheel_support();
    out[3]=(uint64_t)(int64_t)mouse_get_wheel_delta();
    return 0;
}
static uint64_t sys_readsyslog(uint64_t buf,uint64_t sz,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a3;(void)a4;(void)a5;
    return (uint64_t)(int64_t)syslog_read((void*)buf,(uint32_t)sz);
}
static int path_is_ycfs(const char* p) {
    if (p[0] == '/') p++;
    return p[0]=='y'&&p[1]=='c'&&p[2]=='f'&&p[3]=='s'&&(p[4]=='/'||p[4]==0);
}
static uint64_t sys_readdir2(uint64_t path,uint64_t buf,uint64_t max,uint64_t a4,uint64_t a5){
    (void)a4;(void)a5;
    const char* p = (const char*)path;
    if (path_is_ycfs(p)) {
        extern int ycfs_list_dir(const char*, void*, int);
        return (uint64_t)(int64_t)ycfs_list_dir(p, (void*)buf, (int)max);
    }
    return (uint64_t)(int64_t)fat16_list_dir(p,(fat16_entry_t*)buf,(int)max);
}
static uint64_t sys_rename(uint64_t op,uint64_t np,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a3;(void)a4;(void)a5;
    const char* o = (const char*)op;
    if (path_is_ycfs(o)) {
        extern int ycfs_rename(const char*, const char*);
        return (uint64_t)(int64_t)ycfs_rename(o, (const char*)np);
    }
    return (uint64_t)(int64_t)fat16_rename(o,(const char*)np);
}
static uint64_t sys_msgpost(uint64_t name,uint64_t data,uint64_t len,uint64_t a4,uint64_t a5){
    (void)a4;(void)a5;
    return (uint64_t)(int64_t)ipc_post((const char*)name,(const void*)data,(uint32_t)len);
}
static uint64_t sys_msgrecv(uint64_t name,uint64_t data,uint64_t lenp,uint64_t fromp,uint64_t a5){
    (void)a5;
    return (uint64_t)(int64_t)ipc_recv((const char*)name,(void*)data,(uint32_t*)lenp,(uint32_t*)fromp);
}
static uint64_t sys_mqcreate(uint64_t name,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a2;(void)a3;(void)a4;(void)a5;
    return (uint64_t)(int64_t)ipc_create((const char*)name);
}
static uint64_t sys_stat(uint64_t p,uint64_t so,uint64_t io,uint64_t a4,uint64_t a5){
    (void)a4;(void)a5;uint32_t sz=0;uint8_t isd=0;
    const char* path = (const char*)p;
    int r;
    if (path_is_ycfs(path)) {
        extern int ycfs_stat(const char*, uint32_t*, uint8_t*);
        r = ycfs_stat(path,&sz,&isd);
    } else {
        r = fat16_stat(path,&sz,&isd);
    }
    if (r < 0) return (uint64_t)-1ULL;
    if (so) *(uint32_t*)so = sz;
    if (io) *(uint8_t*)io  = isd;
    return 0;
}
static uint64_t sys_mkdir(uint64_t p,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a2;(void)a3;(void)a4;(void)a5;
    const char* path = (const char*)p;
    if (path_is_ycfs(path)) {
        extern int ycfs_mkdir(const char*);
        return (uint64_t)(int64_t)ycfs_mkdir(path);
    }
    return (uint64_t)(int64_t)fat16_mkdir(path);
}
static uint64_t sys_unlink(uint64_t p,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
    (void)a2;(void)a3;(void)a4;(void)a5;
    const char* path = (const char*)p;
    if (path_is_ycfs(path)) {
        extern int ycfs_unlink(const char*);
        return (uint64_t)(int64_t)ycfs_unlink(path);
    }
    return (uint64_t)(int64_t)fat16_unlink(path);
}
static syscall_fn_t syscall_table[SYSCALL_COUNT] = {
    sys_exit, sys_write, sys_read, sys_getpid, sys_yield, sys_sleep,
    sys_open, sys_close, sys_fread,
    sys_shutdown, sys_reboot,
    sys_exec,
    sys_fbinfo, sys_fbwrite,
    sys_keypoll, sys_ticks,
    sys_mouseread,
    sys_readdir,
    sys_savefile,
    sys_stat,
    sys_mkdir,
    sys_unlink,
    sys_msgpost,
    sys_msgrecv,
    sys_mqcreate,
    sys_rename,
    sys_readdir2,
    sys_readcrash,
    sys_readsyslog,
    sys_mousedbg,
    sys_mousewheel,
    sys_set_session_uid,
    sys_youdo,
    sys_chmod,
    sys_chown,
    sys_fileinfo,
    sys_play_pcm,
    sys_pcm_done,
    sys_ac97_debug,
    sys_pcm_can_submit,
    sys_play_stream,
    sys_stream_active,
    sys_get_exec_arg,
    sys_spawn,
    sys_wait_nonblock
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
