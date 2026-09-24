#include <kernel/crash.h>
#include <kernel/ata.h>
#include <kernel/vga.h>
#include <kernel/process.h>
#include <kernel/syslog.h>
#include <stdint.h>

static crash_log_t crash_log;
static int crash_loaded = 0;

static const char* exc_names[] = {
    "Division Error","Debug","NMI","Breakpoint","Overflow",
    "Bound Range","Invalid Opcode","Device NA","Double Fault",
    "Coprocessor Overrun","Invalid TSS","Segment NP","Stack Fault",
    "General Protection Fault","Page Fault","Reserved",
    "FPU Error","Alignment Check","Machine Check","SIMD FP Error"
};

static void u64hex(uint64_t v, char* out) {
    out[16]=0;for(int i=15;i>=0;i--){out[i]="0123456789ABCDEF"[v&0xF];v>>=4;}
}
static void strcopy(char* d, const char* s, int max) {
    int i=0;while(i<max-1&&s[i]){d[i]=s[i];i++;}d[i]=0;
}
static void crash_load(void) {
    if(crash_loaded)return;
    uint8_t buf[512];
    if(ata_read_sectors(CRASH_LOG_LBA,1,buf)==0){
        crash_log_t*cl=(crash_log_t*)buf;
        if(cl->magic==CRASH_MAGIC){
            for(int i=0;i<(int)sizeof(crash_log_t);i++)
                ((uint8_t*)&crash_log)[i]=buf[i];
        } else {
            crash_log.magic=CRASH_MAGIC;
            crash_log.crash_count=0;
        }
    } else {
        crash_log.magic=CRASH_MAGIC;
        crash_log.crash_count=0;
    }
    crash_loaded=1;
}
static void crash_save(void) {
    uint8_t buf[512];
    for(int i=0;i<512;i++)buf[i]=0;
    for(int i=0;i<(int)sizeof(crash_log_t);i++)
        buf[i]=((uint8_t*)&crash_log)[i];
    ata_write_sectors(CRASH_LOG_LBA,1,buf);
}
static void make_summary(crash_log_t*cl, char*out, int maxl) {
    int oi=0;
    #define AP(s) do{const char*_p=(s);while(*_p&&oi<maxl-1)out[oi++]=*_p++;}while(0)
    AP(cl->exc_no<20?exc_names[cl->exc_no]:"Unknown");
    AP(cl->ring==3?" [user":" [kernel");
    AP(cl->proc_name[0]?"/":"/kernel");
    AP(cl->proc_name[0]?cl->proc_name:"");
    AP(cl->recovered?"] RECOVERED":"] HALTED");
    out[oi]=0;
    #undef AP
}

extern uint64_t   scheduler_get_ticks(void);

void crash_init(void) { crash_load(); }

void crash_handle(registers_t* regs) {
    crash_load();
    int ring=(int)(regs->cs&3);

    /* Rotate prev entries */
    for(int i=CRASH_MAX_PREV-1;i>0;i--)
        for(int j=0;j<64;j++)
            crash_log.prev[i][j]=crash_log.prev[i-1][j];
    if(crash_log.crash_count>0)
        make_summary(&crash_log,crash_log.prev[0],64);

    /* Record */
    crash_log.ticks    =scheduler_get_ticks();
    crash_log.exc_no   =(uint32_t)regs->int_no;
    crash_log.ring     =(uint32_t)ring;
    crash_log.rip      =regs->rip;
    crash_log.rsp      =regs->rsp;
    crash_log.rbp      =regs->rbp;
    crash_log.err_code =regs->err_code;
    if(regs->int_no==14){uint64_t cr2;__asm__("mov %%cr2,%0":"=r"(cr2));crash_log.cr2=cr2;}
    else crash_log.cr2=0;
    strcopy(crash_log.exc_name,regs->int_no<20?exc_names[regs->int_no]:"Unknown",32);
    process_t*proc=process_current();
    if(proc)strcopy(crash_log.proc_name,proc->name,32);
    else crash_log.proc_name[0]=0;

    /* Syslog entry */
    char smsg[128];int si=0;
    #define SA2(s) do{const char*_p=(s);while(*_p&&si<127)smsg[si++]=*_p++;}while(0)
    SA2(ring==3?"CRASH ring=3 exc=":"CRASH ring=0 exc=");
    SA2(crash_log.exc_name);
    SA2(" proc=");
    SA2(crash_log.proc_name[0]?crash_log.proc_name:"kernel");
    smsg[si]=0;

    /* Ring-3, real process_create()-spawned child: recover via the
     * actual scheduler. Every real process is now spawned via
     * process_create() (real_exit set unconditionally there), including
     * the fallback shell's exec command -- the last holdout -- so the
     * old legacy longjmp recovery branch that used to sit here is
     * permanently unreachable and has been removed; see the handoff doc
     * for the verification that made this safe. Without this branch, a
     * real child's fault falls through to the ring-0 halt path below
     * and takes down the whole machine over a single process's bug.
     *
     * CORRECTED: a previous investigation's writeup claimed this branch
     * "only ever calls vga_puts_color()" and that the call "genuinely
     * never executes" for a then-unknown reason. Neither half of that
     * was actually true -- this branch has never called vga_puts_color()
     * or anything else that prints, at all; the "[RECOVERED]" string
     * only ever reached crash_save()'s on-disk log and syslog_write()'s
     * entry (see make_summary()/crash_read() and the "syslog"/"crashlog"
     * desktop commands), never anything visible at the moment recovery
     * actually happens. Even a one-shot vga_puts_color() call here
     * wouldn't have reliably fixed that anyway: it writes straight into
     * the single shared framebuffer desktop.c's own compositor repaints
     * wholesale roughly every frame (see fb_put_pixel() in fb.c), so a
     * kernel-level print here would be overwritten before a person could
     * ever see it, on any process that's actually running under
     * desktop's window manager. Real fix lives in sys_exec() instead --
     * see its comment in syscall.c -- surfacing this to the process that
     * actually launched the crashing one (typically an interactive
     * shell.c inside a real "newterm" window), whose own print()s go
     * through that window's persistent per-process I/O, not a one-shot
     * shared-framebuffer draw. */
    if(ring==3&&proc&&proc->real_exit){
        crash_log.recovered=1;
        crash_log.crash_count++;
        crash_save();
        SA2(" [RECOVERED]");
        syslog_write("CRASH",smsg);
        proc->crashed=1;
        proc->state=PROCESS_DEAD;
        process_exit();
        /* unreachable */
    }

    /* Ring-0: log and halt */
    crash_log.recovered=0;
    crash_log.crash_count++;
    crash_save();
    #undef SA2
    syslog_write("PANIC",smsg);

    vga_puts_color("\n[KERNEL PANIC] ",VGA_LIGHT_RED,VGA_BLACK);
    vga_puts_color(crash_log.exc_name,VGA_WHITE,VGA_BLACK);
    char hx[17];
    vga_puts_color(" @ RIP=0x",VGA_YELLOW,VGA_BLACK);
    u64hex(regs->rip,hx);vga_puts_color(hx,VGA_YELLOW,VGA_BLACK);
    if(regs->int_no==14){
        vga_puts_color(" CR2=0x",VGA_YELLOW,VGA_BLACK);
        u64hex(crash_log.cr2,hx);vga_puts_color(hx,VGA_YELLOW,VGA_BLACK);
    }
    vga_puts_color("\nCrash logged to disk. System Halted.\n",VGA_LIGHT_RED,VGA_BLACK);
    __asm__ volatile("cli; hlt");
}

int crash_read(void* buf, uint32_t size) {
    crash_load();
    char* out=(char*)buf; uint32_t oi=0;
    #define AP(s) do{const char*_p=(s);while(*_p&&oi<size-1)out[oi++]=*_p++;}while(0)
    #define APU(v) do{uint64_t _v=(v);char _t[21];int _i=20;_t[20]=0;\
        if(!_v){_t[--_i]='0';}else{while(_v){_t[--_i]='0'+_v%10;_v/=10;}}\
        AP(_t+_i);}while(0)
    #define APX(v) do{char _h[17];u64hex((uint64_t)(v),_h);AP("0x");AP(_h);}while(0)

    AP("=== YouOS Crash Log ===\n");
    AP("Total crashes: ");APU(crash_log.crash_count);AP("\n");
    if(crash_log.crash_count==0){AP("No crashes recorded.\n");out[oi]=0;return(int)oi;}
    AP("\n--- Last Crash ---\n");
    AP("Exception : ");AP(crash_log.exc_name);AP("\n");
    AP("Ring      : ");APU(crash_log.ring);AP("\n");
    AP("Process   : ");AP(crash_log.proc_name[0]?crash_log.proc_name:"(kernel)");AP("\n");
    AP("Ticks     : ");APU(crash_log.ticks);AP("\n");
    AP("RIP       : ");APX(crash_log.rip);AP("\n");
    AP("RSP       : ");APX(crash_log.rsp);AP("\n");
    if(crash_log.exc_no==14){AP("Fault Addr: ");APX(crash_log.cr2);AP("\n");}
    AP("Err Code  : ");APX(crash_log.err_code);AP("\n");
    AP("Recovered : ");AP(crash_log.recovered?"YES - system kept running":"NO - system halted");AP("\n");
    if(crash_log.crash_count>1){
        AP("\n--- Previous Crashes ---\n");
        for(int i=0;i<CRASH_MAX_PREV&&crash_log.prev[i][0];i++){
            AP("  ");APU((uint64_t)(crash_log.crash_count-1-i));AP(". ");
            AP(crash_log.prev[i]);AP("\n");
        }
    }
    out[oi]=0;return(int)oi;
    #undef AP
    #undef APU
    #undef APX
}
