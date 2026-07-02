#include <stdint.h>
#include <kernel/vga.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/pic.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/heap.h>
#include <kernel/terminal.h>
#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/userspace.h>
#include <kernel/initrd.h>
#include <kernel/boot_anim.h>
#include <kernel/elf.h>
#include <kernel/vfs.h>
#include <kernel/fat16.h>
#include <kernel/ata.h>
#include <kernel/kjmp.h>
#include <kernel/fb.h>
#include <kernel/uhci.h>
#include <kernel/ipc.h>
#include <kernel/crash.h>
#include <kernel/syslog.h>
#include <kernel/mouse.h>

#define MULTIBOOT2_MAGIC 0x36D76289

static int kstrcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static void print_uint64(uint64_t val) {
    char buf[21]; int i = 20; buf[i] = '\0';
    if (!val) { vga_puts("0"); return; }
    while (val) { buf[--i] = '0' + (val % 10); val /= 10; }
    vga_puts(&buf[i]);
}

void kernel_main(uint32_t mb2_magic, uint32_t mb2_info) {
    vga_init();
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts_color("                             Welcome to YouOS                                  \n", VGA_YELLOW, VGA_BLACK);
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");

    if (mb2_magic == MULTIBOOT2_MAGIC) {
        vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("Multiboot2 bootloader detected\n");
    } else {
        vga_puts_color("  [!!] ", VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("WARNING: Invalid Multiboot2 magic!\n");
    }
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("VGA driver initialized\n");

    gdt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("GDT loaded\n");

    idt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("IDT loaded\n");

    irq_init();

    /* Program PIT channel 0 to 100Hz (divisor = 1193180/100 = 11931) */
    {
        uint16_t divisor = 11931;
        __asm__ volatile("outb %0, $0x43" :: "a"((uint8_t)0x36));
        __asm__ volatile("outb %0, $0x40" :: "a"((uint8_t)(divisor & 0xFF)));
        __asm__ volatile("outb %0, $0x40" :: "a"((uint8_t)(divisor >> 8)));
    }
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("PIC initialized\n");

    pmm_init((uint64_t)mb2_info);
    pmm_stats_t stats = pmm_get_stats();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("PMM initialized — ");
    print_uint64(stats.free_pages / 256);
    vga_puts(" MB free\n");

    vmm_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("VMM initialized\n");

    /* Parse multiboot2 tags to find framebuffer info */
    {
        uint8_t* p = (uint8_t*)(uint64_t)(mb2_info + 8); /* skip total_size+reserved */
        uint8_t* end = (uint8_t*)(uint64_t)mb2_info
                     + *(uint32_t*)(uint64_t)mb2_info;
        while (p < end) {
            uint32_t type = *(uint32_t*)p;
            uint32_t size = *(uint32_t*)(p + 4);
            if (type == 8) { /* framebuffer tag */
                uint64_t fb_addr  = *(uint64_t*)(p + 8);
                uint32_t fb_pitch = *(uint32_t*)(p + 16);
                uint32_t fb_w     = *(uint32_t*)(p + 20);
                uint32_t fb_h     = *(uint32_t*)(p + 24);
                uint8_t  fb_bpp   = *(uint8_t* )(p + 28);
                /* Map framebuffer into virtual address space */
                /* Framebuffer may be above 1GB identity map — map it explicitly */
                uint64_t fb_pages = (fb_h * fb_pitch + 4095) / 4096 + 1;
                for (uint64_t pg = 0; pg < fb_pages; pg++) {
                    uint64_t pa = (fb_addr & ~(uint64_t)0xFFF) + pg * 4096;
                    vmm_map(&kernel_as, pa, pa,
                            PTE_PRESENT | PTE_WRITABLE);
                }
                fb_init(fb_addr, fb_w, fb_h, fb_pitch, fb_bpp);
                fb_terminal_init();
                vga_puts_color("  [OK] Framebuffer: ", VGA_LIGHT_GREEN, VGA_BLACK);
                print_uint64(fb_w); vga_puts("x");
                print_uint64(fb_h); vga_puts("x");
                print_uint64(fb_bpp); vga_puts("bpp\n");
                break;
            }
            if (type == 0) break; /* end tag */
            p += (size + 7) & ~7; /* align to 8 bytes */
        }
    }

    heap_init();
    ata_init();
    int fat16_ok = fat16_init();
    syslog_init();
    crash_init();
    ipc_init();
    uhci_init();
    initrd_init();
    vfs_init();
    extern vfs_node_t* ramfs_init(void);
    vfs_mount_root(ramfs_init());
    /* Mount FAT16 as /disk */
    {
        vfs_node_t* disk = fat16_vfs_mount();
        if (disk) {
            vfs_node_t* root = vfs_resolve("/");
            if (root) {
                disk->next = (vfs_node_t*)root->fs_data;
                root->fs_data = disk;
            }
            vga_puts_color("  [OK] /disk mounted (FAT16)\n", VGA_LIGHT_GREEN, VGA_BLACK);
        }
    }
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Heap initialized\n");

    terminal_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Keyboard driver loaded\n");

    scheduler_init();
    pic_unmask(IRQ_TIMER);
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Scheduler initialized\n");

    pic_unmask(IRQ_KEYBOARD);
    pic_unmask(2);
    mouse_init();
    pic_unmask(12);

    syscall_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Syscall interface initialized (SYSCALL/SYSRET)\n");

    vga_puts("\n");
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    boot_anim_run();

    /* Auto-launch user shell */
    {
        uint64_t sz = 0;
        const void* sd = initrd_find("desktop", &sz);
        if (!sd) {
            vga_puts_color("  [!!] desktop not found in initrd, falling back to shell\n", VGA_LIGHT_GREEN, VGA_BLACK);
            sd = initrd_find("shell", &sz);
        }
        if (sd) {
            elf_load_result_t r;
            address_space_t pa = vmm_create_user_as();
            if (elf_load(&pa, sd, sz, &r) == 0) {
                uint64_t sb = pmm_alloc_pages(4);
                uint64_t st = sb + 4 * PAGE_SIZE;
                for (uint64_t a = sb; a < st; a += 4096)
                    vmm_map(&pa, a, a, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
                static uint8_t sk[8192];
                extern void tss_set_kernel_stack(uint64_t);
                tss_set_kernel_stack((uint64_t)sk + sizeof(sk));
                vmm_switch(&pa);
                __asm__ volatile("mov %%cr3,%%rax;mov %%rax,%%cr3":::"rax","memory");
                extern kjmp_buf_t kernel_exit_jmp;
                extern int kernel_exit_jmp_valid;
                kernel_exit_jmp_valid = 1;
                extern void jump_to_userspace(uint64_t, uint64_t);
                if (!ksetjmp(&kernel_exit_jmp))
                    jump_to_userspace(r.entry, st);
                vmm_switch(&kernel_as);
            }
        }
    }
    vga_puts_color("  YouOS shell — type 'help' for commands\n", VGA_WHITE, VGA_BLACK);
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");

    char line[256];
    while (1) {
        vga_puts_color("YouOS> ", VGA_LIGHT_GREEN, VGA_BLACK);
        terminal_readline(line, sizeof(line));

        if (kstrcmp(line, "help") == 0) {
            vga_puts_color("  Commands:\n", VGA_LIGHT_CYAN, VGA_BLACK);
            vga_puts("    help      - show this message\n");
            vga_puts("    clear     - clear the screen\n");
            vga_puts("    mem       - show memory stats\n");
            vga_puts("    heap      - show heap stats\n");
            vga_puts("    ps        - list processes\n");
            vga_puts("    version   - show YouOS version\n");
            vga_puts("    userspace - run Ring 3 program\n");
            vga_puts("    exec <name> - run ELF from initrd\n");
            vga_puts("    disk     - read sector 0 from ATA disk\n");
            vga_puts("    diskcat  - read file from FAT16 disk\n");
            vga_puts("    diskwrite- write file to FAT16 disk\n");
            vga_puts("    reboot   - reboot the system\n");
            vga_puts("    shutdown - power off\n");

        } else if (kstrcmp(line, "clear") == 0) {
            vga_clear();

        } else if (kstrcmp(line, "mem") == 0) {
            pmm_stats_t s = pmm_get_stats();
            vga_puts_color("  Memory:\n", VGA_LIGHT_CYAN, VGA_BLACK);
            vga_puts("    Free  : "); print_uint64(s.free_pages/256); vga_puts(" MB\n");
            vga_puts("    Used  : "); print_uint64(s.used_pages/256); vga_puts(" MB\n");
            vga_puts("    Total : "); print_uint64(s.total_pages/256); vga_puts(" MB\n");

        } else if (kstrcmp(line, "heap") == 0) {
            heap_dump_stats();

        } else if (kstrcmp(line, "ps") == 0) {
            vga_puts_color("  PID  STATE    NAME\n", VGA_LIGHT_CYAN, VGA_BLACK);
            for (uint32_t pid = 0; pid <= 4; pid++) {
                process_t* p = process_get(pid);
                if (!p) continue;
                vga_puts("  "); print_uint64(p->pid);
                vga_puts("    ");
                const char* states[] = {"READY  ","RUNNING","SLEEP  ","DEAD   "};
                vga_puts(states[p->state]);
                vga_puts("  "); vga_puts(p->name); vga_puts("\n");
            }

        } else if (kstrcmp(line, "version") == 0) {
            vga_puts_color("  YouOS v0.1.0\n", VGA_YELLOW, VGA_BLACK);
            vga_puts("  Architecture : x86_64\n");
            vga_puts("  Built from scratch — no Linux\n");

        } else if (kstrcmp(line, "reboot") == 0) {
            vga_puts_color("  Rebooting...\n", VGA_YELLOW, VGA_BLACK);
            /* Pulse the 8042 keyboard controller reset line */
            __asm__ volatile("cli");
            /* Wait for keyboard controller ready */
            uint8_t tmp;
            do { __asm__ volatile("inb $0x64, %0" : "=a"(tmp)); } while (tmp & 0x02);
            __asm__ volatile("outb %0, $0x64" : : "a"((uint8_t)0xFE));
            /* If that didn't work, triple fault */
            __asm__ volatile("cli; hlt");
        } else if (kstrcmp(line, "shutdown") == 0) {
            vga_puts_color("  Shutting down...\n", VGA_YELLOW, VGA_BLACK);
            /* QEMU/Bochs debug exit port */
            __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
            /* ACPI shutdown via port 0xB004 (older QEMU) */
            __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
            /* Halt if nothing worked */
            __asm__ volatile("cli; hlt");
        } else if (kstrcmp(line, "disk") == 0) {
            static uint8_t sector_buf[512];
            if (ata_read_sectors(0, 1, sector_buf) == 0) {
                vga_puts_color("  Sector 0: ", VGA_YELLOW, VGA_BLACK);
                /* Print first 16 bytes as hex + ASCII */
                for (int i = 0; i < 16; i++) {
                    char hx[3];
                    hx[0] = "0123456789ABCDEF"[sector_buf[i] >> 4];
                    hx[1] = "0123456789ABCDEF"[sector_buf[i] & 0xF];
                    hx[2] = 0;
                    vga_puts(hx); vga_puts(" ");
                }
                vga_puts("  |");
                for (int i = 0; i < 16; i++) {
                    char c[2];
                    c[0] = (sector_buf[i] >= 32 && sector_buf[i] < 127) ? sector_buf[i] : '.';
                    c[1] = 0;
                    vga_puts(c);
                }
                vga_puts("|\n");
            } else {
                vga_puts_color("  [!!] disk read failed\n", VGA_LIGHT_RED, VGA_BLACK);
            }
        } else if (line[0]=='e' && line[1]=='x' && line[2]=='e' && line[3]=='c' && line[4]==' ') {
            const char* name = line + 5;
            uint64_t elf_size = 0;
            const void* elf_data = initrd_find(name, &elf_size);
            if (!elf_data) {
                vga_puts_color("  [!!] Not found: ", VGA_LIGHT_RED, VGA_BLACK);
                vga_puts(name); vga_puts("\n");
            } else {
                elf_load_result_t res;
                /* Create isolated address space for this process */
                address_space_t proc_as = vmm_create_user_as();
                vmm_switch(&proc_as);
                if (elf_load(&proc_as, elf_data, elf_size, &res) == 0) {
                    extern void tss_set_kernel_stack(uint64_t);
                    static uint8_t elf_kstack[8192];
                    tss_set_kernel_stack((uint64_t)elf_kstack + sizeof(elf_kstack));
                    uint64_t stack_base = pmm_alloc_pages(4);
                    uint64_t stack_top  = stack_base + 4 * PAGE_SIZE;
                    for (uint64_t a = stack_base; a < stack_top; a += 4096)
                        vmm_map(&proc_as, a, a, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
                    /* Flush TLB */
                    __asm__ volatile("mov %%cr3,%%rax; mov %%rax,%%cr3":::"rax","memory");
                    vga_puts_color("  [OK] Jumping to ELF entry...\n", VGA_LIGHT_GREEN, VGA_BLACK);
                    extern void jump_to_userspace(uint64_t entry, uint64_t stack);
                    extern kjmp_buf_t kernel_exit_jmp;
                    extern int kernel_exit_jmp_valid;
                    kernel_exit_jmp_valid = 1;
                    if (!ksetjmp(&kernel_exit_jmp)) {
                        jump_to_userspace(res.entry, stack_top);
                    }
                    /* Restore kernel address space */
                    vmm_switch(&kernel_as);
                    vga_puts_color("  [OK] Process exited\n", VGA_LIGHT_GREEN, VGA_BLACK);
                }
            }
        } else if (line[0]=='d'&&line[1]=='i'&&line[2]=='s'&&line[3]=='k'&&line[4]=='c'&&line[5]=='a'&&line[6]=='t'&&line[7]==' ') {
            const char* fname = line + 8;
            int fd = fat16_open(fname);
            if (fd < 0) {
                vga_puts_color("  [!!] File not found on disk\n", VGA_LIGHT_RED, VGA_BLACK);
            } else {
                char buf[256];
                int n;
                while ((n = fat16_read(fd, buf, 255)) > 0) {
                    buf[n] = 0;
                    vga_puts(buf);
                }
                fat16_close(fd);
                vga_puts("\n");
            }
        } else if (line[0]=='d'&&line[1]=='i'&&line[2]=='s'&&line[3]=='k'&&line[4]=='w'&&line[5]=='r'&&line[6]=='i'&&line[7]=='t'&&line[8]=='e'&&line[9]==' ') {
            /* diskwrite filename content */
            const char* rest = line + 10;
            /* find space between filename and content */
            int sp = 0;
            while (rest[sp] && rest[sp] != ' ') sp++;
            if (rest[sp] == ' ') {
                char fname[64];
                for (int k = 0; k < sp && k < 63; k++) fname[k] = rest[k];
                fname[sp] = 0;
                const char* content = rest + sp + 1;
                int fd = fat16_create(fname);
                if (fd < 0) {
                    vga_puts_color("  [!!] Could not create file\n", VGA_LIGHT_RED, VGA_BLACK);
                } else {
                    int len = 0;
                    while (content[len]) len++;
                    fat16_write(fd, content, len);
                    fat16_close(fd);
                    vga_puts_color("  [OK] Written\n", VGA_LIGHT_GREEN, VGA_BLACK);
                }
            }
        } else if (line[0]=='d'&&line[1]=='i'&&line[2]=='s'&&line[3]=='k'&&line[4]=='c'&&line[5]=='a'&&line[6]=='t'&&line[7]==' ') {
            const char* fname = line + 8;
            int fd = fat16_open(fname);
            if (fd < 0) {
                vga_puts_color("  [!!] File not found on disk\n", VGA_LIGHT_RED, VGA_BLACK);
            } else {
                char buf[256];
                int n;
                while ((n = fat16_read(fd, buf, 255)) > 0) {
                    buf[n] = 0;
                    vga_puts(buf);
                }
                fat16_close(fd);
                vga_puts("\n");
            }
        } else if (line[0]=='d'&&line[1]=='i'&&line[2]=='s'&&line[3]=='k'&&line[4]=='w'&&line[5]=='r'&&line[6]=='i'&&line[7]=='t'&&line[8]=='e'&&line[9]==' ') {
            /* diskwrite filename content */
            const char* rest = line + 10;
            /* find space between filename and content */
            int sp = 0;
            while (rest[sp] && rest[sp] != ' ') sp++;
            if (rest[sp] == ' ') {
                char fname[64];
                for (int k = 0; k < sp && k < 63; k++) fname[k] = rest[k];
                fname[sp] = 0;
                const char* content = rest + sp + 1;
                int fd = fat16_create(fname);
                if (fd < 0) {
                    vga_puts_color("  [!!] Could not create file\n", VGA_LIGHT_RED, VGA_BLACK);
                } else {
                    int len = 0;
                    while (content[len]) len++;
                    fat16_write(fd, content, len);
                    fat16_close(fd);
                    vga_puts_color("  [OK] Written\n", VGA_LIGHT_GREEN, VGA_BLACK);
                }
            }
        } else if (kstrcmp(line, "userspace") == 0) {
            extern void hello_main(void);
            extern void tss_set_kernel_stack(uint64_t stack);
            static uint8_t ring3_kstack[8192];
            tss_set_kernel_stack((uint64_t)ring3_kstack + sizeof(ring3_kstack));
            vga_puts_color("  Creating Ring 3 process...\n", VGA_YELLOW, VGA_BLACK);
            user_process_t* proc = user_process_create("hello", hello_main);
            if (!proc) {
                vga_puts_color("  [!!] Failed\n", VGA_LIGHT_RED, VGA_BLACK);
            } else {
                vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
                vga_puts("Jumping to Ring 3...\n");
                vga_puts_color("  ----------------------------------------\n", VGA_DARK_GREY, VGA_BLACK);
                extern kjmp_buf_t kernel_exit_jmp;
                extern int kernel_exit_jmp_valid;
                kernel_exit_jmp_valid = 1;
                if (!ksetjmp(&kernel_exit_jmp)) {
                    user_process_exec(proc);
                }
                vga_puts_color("  ----------------------------------------\n", VGA_DARK_GREY, VGA_BLACK);
                vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
                vga_puts("Returned from Ring 3\n");
                user_process_destroy(proc);
            }

        } else if (line[0] != '\0') {
            vga_puts_color("  Unknown command: ", VGA_LIGHT_RED, VGA_BLACK);
            vga_puts(line);
            vga_puts("\n  Type 'help' for available commands.\n");
        }
    }
}
