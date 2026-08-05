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
#include <kernel/pci.h>
#include <kernel/rtl8139.h>
#include <kernel/ac97.h>
#include <kernel/arp.h>
#include <kernel/ip.h>
#include <kernel/udp.h>
#include <kernel/tcp.h>
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

    /* Only enable hardware interrupts now that irq_init() has remapped
     * the PIC off its default 0x08-0x0F vector range — enabling them any
     * earlier risked a routine IRQ (e.g. the timer) landing on vector
     * 0x08, colliding with the CPU's own Double Fault vector and being
     * misreported as a crash. This was an intermittent, KVM-only boot
     * failure (real elapsed time made the race far more likely to be
     * hit than under TCG's slower emulation). */
    extern void sti_enable(void);
    sti_enable();

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

    /* Isolated vmm_get_phys() self-test — added to diagnose a real
     * bug found this session: a heap allocation right after
     * heap_init() (e.g. 0xFFFF800000001000, the very first block)
     * fails to resolve via vmm_get_phys() even though the memory is
     * genuinely readable/writable (a copy into it succeeds without
     * faulting). This isolates the question to just heap + vmm,
     * removing AC97/audio/everything else from the picture. */
    {
        extern void* kmalloc(size_t);
        extern address_space_t kernel_as;
        void* test_ptr = kmalloc(64);
        if (test_ptr) {
            uint64_t virt = (uint64_t)test_ptr;
            uint64_t page_virt = virt & ~(uint64_t)0xFFF;
            uint64_t phys = vmm_get_phys(&kernel_as, page_virt);
            if (phys) {
                vga_puts_color("  [OK] vmm_get_phys self-test: heap addr resolved\n", VGA_LIGHT_GREEN, VGA_BLACK);
            } else {
                vga_puts_color("  [!!] vmm_get_phys self-test: heap addr FAILED to resolve\n", VGA_YELLOW, VGA_BLACK);
            }
        } else {
            vga_puts_color("  [!!] vmm_get_phys self-test: kmalloc(64) failed\n", VGA_YELLOW, VGA_BLACK);
        }
    }

    ata_init();
    int fat16_ok = fat16_init();
    syslog_init();
    crash_init();
    ipc_init();
    pci_init();
    uhci_init();
    if (ac97_init()) {
        /* Self-test: synthesize a short, quiet square-wave tone and
         * play it, then confirm the completion interrupt actually
         * fires within a timeout — proof DMA + IRQ genuinely work,
         * same standard as RTL8139's stage-1 self-test. Doesn't (and
         * can't, in an automated boot log) confirm audible correctness
         * — that's a manual listening check, not something to gate
         * the self-test's pass/fail on. */
        static int16_t tone[800];
        for (int i = 0; i < 800; i++) tone[i] = (int16_t)((i / 20) % 2 ? 3000 : -3000);
        extern uint64_t irq_get_ticks(void);
        ac97_play_pcm(tone, 800, 44100, 1);
        uint64_t tone_start = irq_get_ticks();
        int tone_ok = 0;
        while ((irq_get_ticks() - tone_start) < 100) {
            if (ac97_is_done()) { tone_ok = 1; break; }
        }
        if (tone_ok) {
            vga_puts_color("  [OK] AC97 self-test: playback interrupt fired\n", VGA_LIGHT_GREEN, VGA_BLACK);
        } else {
            vga_puts_color("  [!!] AC97 self-test: no playback interrupt\n", VGA_YELLOW, VGA_BLACK);
        }
    }
    if (rtl8139_init()) {
        /* Static placeholder — QEMU user-net's default guest address.
         * Real DHCP is a future stage; nothing here negotiates this.
         * Single shared source of truth, passed to both arp_init() and
         * ip_init() rather than each module guessing its own copy. */
        static const uint8_t our_static_ip[4] = {10, 0, 2, 15};
        static const uint8_t our_netmask[4]   = {255, 255, 255, 0};
        static const uint8_t our_gateway[4]   = {10, 0, 2, 2};
        arp_init(our_static_ip);
        /* Self-test: ARP-probe the QEMU user-mode gateway (10.0.2.2)
         * and confirm the reply actually gets parsed into the ARP
         * cache — a real test of stage 2's parsing, not just "an
         * interrupt fired" (stage 1's weaker self-test). */
        static const uint8_t gateway_ip[4] = {10,0,2,2};
        arp_request(gateway_ip);
        extern uint64_t irq_get_ticks(void);
        uint64_t start_tick = irq_get_ticks();
        uint8_t mac_out[6];
        int resolved = 0;
        while ((irq_get_ticks() - start_tick) < 50) {
            net_poll();
            if (arp_resolve(gateway_ip, mac_out)) { resolved = 1; break; }
        }
        if (resolved) {
            vga_puts_color("  [OK] RTL8139/ARP self-test: gateway resolved\n", VGA_LIGHT_GREEN, VGA_BLACK);

            /* IP/ICMP self-test — reuses the gateway MAC just resolved
             * above, no extra ARP round-trip needed. Sends a real ICMP
             * echo request and waits for the actual reply to come back
             * through ip_handle_frame(), same "prove it end-to-end"
             * standard stage 2 used for ARP. */
            ip_init(our_static_ip, our_netmask, our_gateway);
            uint8_t icmp_pkt[12];
            icmp_pkt[0] = 8;  /* type: echo request */
            icmp_pkt[1] = 0;  /* code */
            icmp_pkt[2] = 0; icmp_pkt[3] = 0; /* checksum, filled below */
            icmp_pkt[4] = 0; icmp_pkt[5] = 1; /* id = 1 */
            icmp_pkt[6] = 0; icmp_pkt[7] = 1; /* seq = 1 */
            icmp_pkt[8]='P'; icmp_pkt[9]='I'; icmp_pkt[10]='N'; icmp_pkt[11]='G';
            /* Internet checksum over the whole ICMP packet, checksum field zeroed */
            {
                uint32_t sum = 0;
                for (int i = 0; i < 12; i += 2) {
                    uint16_t word = (uint16_t)((icmp_pkt[i] << 8) | (i+1<12 ? icmp_pkt[i+1] : 0));
                    sum += word;
                }
                while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
                uint16_t csum = (uint16_t)~sum;
                icmp_pkt[2] = (uint8_t)(csum >> 8);
                icmp_pkt[3] = (uint8_t)(csum & 0xFF);
            }
            static const uint8_t gw2[4] = {10,0,2,2};
            ip_send(gw2, IP_PROTO_ICMP, icmp_pkt, sizeof(icmp_pkt));

            extern volatile int icmp_echo_reply_seen;
            uint64_t ping_start = irq_get_ticks();
            int ping_ok = 0;
            while ((irq_get_ticks() - ping_start) < 50) {
                net_poll();
                if (icmp_echo_reply_seen) { ping_ok = 1; break; }
            }
            if (ping_ok) {
                vga_puts_color("  [OK] IP/ICMP self-test: ping reply received\n", VGA_LIGHT_GREEN, VGA_BLACK);
            } else {
                vga_puts_color("  [!!] IP/ICMP self-test: no ping reply\n", VGA_YELLOW, VGA_BLACK);
            }

            if (ping_ok) {
                /* UDP self-test: a minimal, real DNS query to QEMU
                 * SLIRP's built-in DNS proxy (10.0.2.3:53) — this is
                 * a genuine external UDP service, same "real wire
                 * round-trip" standard as the ARP/ICMP self-tests, not
                 * a loopback trick. Doesn't parse the DNS answer at
                 * all, just proves the round trip: our checksum was
                 * accepted, the reply's source/dest ports line up,
                 * and udp_handle_packet() correctly dispatches it. */
                static const uint8_t dns_proxy_ip[4] = {10, 0, 2, 3};
                uint8_t dns_query[29] = {
                    0x12, 0x34,             /* transaction id */
                    0x01, 0x00,             /* flags: standard query, recursion desired */
                    0x00, 0x01,             /* qdcount = 1 */
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* an/ns/ar counts = 0 */
                    7,'e','x','a','m','p','l','e',
                    3,'c','o','m',
                    0,                       /* end of QNAME */
                    0x00, 0x01,             /* qtype: A */
                    0x00, 0x01              /* qclass: IN */
                };
                extern volatile int udp_packet_seen;
                uint64_t udp_start = irq_get_ticks();
                int udp_ok = 0;
                int udp_sent = (udp_send(dns_proxy_ip, 53, 5353, dns_query, sizeof(dns_query)) == 0);
                while ((irq_get_ticks() - udp_start) < 50) {
                    net_poll();
                    /* Destination MAC likely wasn't cached on the first
                     * call (unlike the gateway, already resolved by the
                     * earlier ARP self-test) — ip_send() returns -1 and
                     * fires an arp_request() as a side effect in that
                     * case; retry the actual send once resolution has
                     * had a chance to land via the net_poll() above. */
                    if (!udp_sent)
                        udp_sent = (udp_send(dns_proxy_ip, 53, 5353, dns_query, sizeof(dns_query)) == 0);
                    if (udp_packet_seen) { udp_ok = 1; break; }
                }
                if (udp_ok) {
                    vga_puts_color("  [OK] UDP self-test: DNS reply received\n", VGA_LIGHT_GREEN, VGA_BLACK);

                    /* TCP self-test (stages 5a+5b combined): connect to
                     * a real external host (1.1.1.1:80, Cloudflare —
                     * chosen for high uptime/stable IP, no DNS
                     * resolution needed), attempt a real HTTP/1.0 GET,
                     * read back real response bytes, then close (either
                     * the server closes first via its own FIN, handled
                     * by tcp.c's passive-close path, or we close as a
                     * fallback). Depends on genuine internet
                     * reachability through QEMU SLIRP — an accepted
                     * external dependency for this stage. Nested inside
                     * this if(udp_ok) block so udp_ok stays in scope. */
                    tcp_init();
                    static const uint8_t remote[4] = {1, 1, 1, 1};
                    tcp_connect(remote, 80);

                    uint64_t tcp_start = irq_get_ticks();
                    int reached_established = 0;
                    while ((irq_get_ticks() - tcp_start) < 100) {
                        net_poll();
                        if (tcp_get_state() == TCP_ESTABLISHED) { reached_established = 1; break; }
                        if (tcp_get_state() == TCP_CLOSED) break; /* RST or similar failure */
                    }

                    if (reached_established) {
                        static const char http_get[] = "GET / HTTP/1.0\r\nHost: 1.1.1.1\r\n\r\n";
                        tcp_send(http_get, (uint16_t)(sizeof(http_get) - 1));

                        static uint8_t http_resp[512];
                        uint16_t total_got = 0;
                        uint64_t data_start = irq_get_ticks();
                        while ((irq_get_ticks() - data_start) < 150) {
                            net_poll();
                            uint16_t n = tcp_recv(http_resp + total_got, (uint16_t)(sizeof(http_resp) - total_got));
                            total_got = (uint16_t)(total_got + n);
                            if (total_got > 0 && tcp_get_state() == TCP_CLOSED) break;
                            if (total_got >= sizeof(http_resp) - 1) break;
                        }

                        /* If the server hasn't already closed the
                         * connection itself, close it ourselves as a
                         * fallback rather than leaving it dangling. */
                        if (tcp_get_state() == TCP_ESTABLISHED) {
                            tcp_close();
                            uint64_t close_start = irq_get_ticks();
                            while ((irq_get_ticks() - close_start) < 100) {
                                net_poll();
                                if (tcp_get_state() != TCP_ESTABLISHED && tcp_get_state() != TCP_FIN_WAIT_1) break;
                            }
                        }

                        if (total_got > 0) {
                            http_resp[total_got < sizeof(http_resp) ? total_got : sizeof(http_resp)-1] = 0;
                            /* Log only the first line (HTTP status line)
                             * — the full response could be arbitrarily
                             * large/binary, one clean line is enough
                             * proof. */
                            char status_line[64];
                            int si = 0;
                            while (si < 63 && http_resp[si] && http_resp[si] != '\r') { status_line[si] = (char)http_resp[si]; si++; }
                            status_line[si] = 0;
                            syslog_write("TCP", status_line);
                            vga_puts_color("  [OK] TCP self-test: HTTP response received\n", VGA_LIGHT_GREEN, VGA_BLACK);
                        } else {
                            vga_puts_color("  [!!] TCP self-test: no HTTP response data\n", VGA_YELLOW, VGA_BLACK);
                        }
                    } else {
                        vga_puts_color("  [!!] TCP self-test: handshake did not complete\n", VGA_YELLOW, VGA_BLACK);
                    }
                } else {
                    vga_puts_color("  [!!] UDP self-test: no DNS reply\n", VGA_YELLOW, VGA_BLACK);
                }
            }
        } else {
            vga_puts_color("  [!!] RTL8139/ARP self-test: no reply parsed (check -netdev flags)\n", VGA_YELLOW, VGA_BLACK);
        }
    }
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

    /* Mount YCFS as /ycfs (phase 1: read-only, lives in a fixed 16MB
     * region 64MB into disk.img — see include/kernel/ycfs.h) */
    {
        extern vfs_node_t* ycfs_vfs_mount(void);
        vfs_node_t* yc = ycfs_vfs_mount();
        if (yc) {
            vfs_node_t* root = vfs_resolve("/");
            if (root) {
                yc->next = (vfs_node_t*)root->fs_data;
                root->fs_data = yc;
            }
            vga_puts_color("  [OK] /ycfs mounted\n", VGA_LIGHT_GREEN, VGA_BLACK);

            /* Phase 3 self-test: deliberately simulates a crash mid-
             * transaction and a crash after-commit-but-before-real-write,
             * proving journal replay makes the correct call in both cases. */
            extern int ycfs_journal_self_test(void);
            if (ycfs_journal_self_test() == 0) {
                syslog_write("YCFS", "journal self-test OK - incomplete txn discarded, committed txn replayed");
                vga_puts_color("  [OK] YCFS journal self-test passed\n", VGA_LIGHT_GREEN, VGA_BLACK);
            } else {
                syslog_write("YCFS", "journal self-test FAILED");
                vga_puts_color("\n\n  [FATAL] YCFS JOURNAL SELF-TEST FAILED — replay logic is broken.\n", VGA_LIGHT_RED, VGA_BLACK);
                vga_puts_color("  System halted so this can't be missed. Check ycfs_journal_self_test()\n", VGA_LIGHT_RED, VGA_BLACK);
                vga_puts_color("  and ycfs_journal_replay() in kernel/fs/ycfs.c.\n", VGA_LIGHT_RED, VGA_BLACK);
                while (1) { __asm__ volatile("hlt"); }
            }

            /* Self-test: read the seed files mkycfs.py wrote, including
             * through a nested directory, to confirm real path traversal
             * and multi-level finddir() work end-to-end. Non-fatal. */
            int fd1 = vfs_open("/ycfs/hello.txt", 0);
            if (fd1 >= 0) {
                char buf[128]; int n = 0;
                uint64_t r = vfs_read(fd1, buf, sizeof(buf) - 1);
                n = (int)r; buf[n] = 0;
                vfs_close(fd1);
                vga_puts_color("  [OK] YCFS self-test: /ycfs/hello.txt -> ", VGA_LIGHT_GREEN, VGA_BLACK);
                vga_puts_color(buf, VGA_WHITE, VGA_BLACK);
                syslog_write("YCFS", "hello.txt OK");
                syslog_write("YCFS", buf);
            } else {
                vga_puts_color("  [!!] YCFS self-test: /ycfs/hello.txt not found\n", VGA_LIGHT_RED, VGA_BLACK);
                syslog_write("YCFS", "hello.txt FAILED - not found");
            }
            int fd2 = vfs_open("/ycfs/docs/notes.txt", 0);
            if (fd2 >= 0) {
                char buf[128]; int n = 0;
                uint64_t r = vfs_read(fd2, buf, sizeof(buf) - 1);
                n = (int)r; buf[n] = 0;
                vfs_close(fd2);
                vga_puts_color("  [OK] YCFS self-test: /ycfs/docs/notes.txt -> ", VGA_LIGHT_GREEN, VGA_BLACK);
                vga_puts_color(buf, VGA_WHITE, VGA_BLACK);
                syslog_write("YCFS", "docs/notes.txt OK (nested dirs work)");
                syslog_write("YCFS", buf);
            } else {
                vga_puts_color("  [!!] YCFS self-test: /ycfs/docs/notes.txt not found (nested dir traversal failed)\n", VGA_LIGHT_RED, VGA_BLACK);
                syslog_write("YCFS", "docs/notes.txt FAILED - nested dir traversal broken");
            }
            /* Phase 2 self-test DISABLED — was running unconditionally on
             * every boot, allocating/freeing blocks and inodes every time,
             * with its own content-write call happening OUTSIDE any
             * txn_begin()/txn_commit() (so it wasn't journaled). Suspected
             * of corrupting unrelated live data via stale journal replay
             * on reused block numbers — disabled while under investigation.
             * See project notes on the auth.dat persistence bug. */
            #if 0
            /* Phase 2 self-test: create a new file and a new subdirectory
             * under root, write content, read it back, and confirm the
             * subdirectory is findable — all non-fatal, logged to syslog. */
            extern int ycfs_create(uint32_t, const char*, uint32_t, uint32_t*);
            extern int64_t ycfs_write(uint32_t, uint64_t, uint32_t, const void*);
            extern uint32_t ycfs_root_inode(void);
            #define YCFS_TYPE_FILE_ 1
            #define YCFS_TYPE_DIR_  2
            uint32_t new_file_inode = 0, new_dir_inode = 0;
            const char* write_msg = "Phase 2 write test - if you can read this, writes work.\n";
            int create_ok = ycfs_create(ycfs_root_inode(), "written.txt", YCFS_TYPE_FILE_, &new_file_inode);
            int mkdir_ok  = ycfs_create(ycfs_root_inode(), "newdir", YCFS_TYPE_DIR_, &new_dir_inode);
            if (create_ok == 0) {
                int64_t wn = ycfs_write(new_file_inode, 0, (uint32_t)(sizeof("Phase 2 write test - if you can read this, writes work.\n") - 1), write_msg);
                (void)wn;
                int fd3 = vfs_open("/ycfs/written.txt", 0);
                if (fd3 >= 0) {
                    char buf[128];
                    uint64_t r = vfs_read(fd3, buf, sizeof(buf) - 1);
                    buf[(int)r] = 0;
                    vfs_close(fd3);
                    syslog_write("YCFS", "write test OK");
                    syslog_write("YCFS", buf);
                } else {
                    syslog_write("YCFS", "write test FAILED - could not reopen written.txt");
                }
            } else {
                syslog_write("YCFS", "write test FAILED - ycfs_create(written.txt) failed");
            }
            if (mkdir_ok == 0) {
                int fd4 = vfs_open("/ycfs/newdir", 0);
                if (fd4 >= 0) {
                    vfs_close(fd4);
                    syslog_write("YCFS", "mkdir test OK - /ycfs/newdir findable");
                } else {
                    syslog_write("YCFS", "mkdir test FAILED - /ycfs/newdir not findable after create");
                }
            } else {
                syslog_write("YCFS", "mkdir test FAILED - ycfs_create(newdir) failed");
            }

            /* Phase 2b self-test: list_dir, rename (into a nested dir,
             * proving cross-directory moves work), then unlink. */
            extern int ycfs_list_dir(const char*, void*, int);
            extern int ycfs_rename(const char*, const char*);
            extern int ycfs_unlink(const char*);
            struct { char name[32]; uint32_t size; uint8_t is_dir; } list_buf[16];
            int list_n = ycfs_list_dir("/ycfs", list_buf, 16);
            if (list_n >= 3) { /* hello.txt, docs, newdir, written.txt at least */
                syslog_write("YCFS", "list_dir test OK - /ycfs root listed");
            } else {
                syslog_write("YCFS", "list_dir test FAILED - unexpected entry count");
            }

            int rename_ok = ycfs_rename("/ycfs/written.txt", "/ycfs/newdir/moved.txt");
            if (rename_ok == 0) {
                int fd5 = vfs_open("/ycfs/newdir/moved.txt", 0);
                if (fd5 >= 0) {
                    vfs_close(fd5);
                    syslog_write("YCFS", "rename test OK - moved into nested dir");
                } else {
                    syslog_write("YCFS", "rename test FAILED - moved.txt not findable at new path");
                }
            } else {
                syslog_write("YCFS", "rename test FAILED - ycfs_rename returned error");
            }

            int unlink_ok = ycfs_unlink("/ycfs/newdir/moved.txt");
            if (unlink_ok == 0) {
                int fd6 = vfs_open("/ycfs/newdir/moved.txt", 0);
                if (fd6 < 0) {
                    syslog_write("YCFS", "unlink test OK - moved.txt gone");
                } else {
                    vfs_close(fd6);
                    syslog_write("YCFS", "unlink test FAILED - moved.txt still findable");
                }
            } else {
                syslog_write("YCFS", "unlink test FAILED - ycfs_unlink returned error");
            }
            #endif /* Phase 2 self-test disabled */
        } else {
            vga_puts_color("  [!!] YCFS mount failed (did you run mkycfs.py?)\n", VGA_LIGHT_RED, VGA_BLACK);
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
