bits 32

section .multiboot2
align 8
mb2_start:
    dd 0xE85250D6
    dd 0
    dd mb2_end - mb2_start
    dd 0x100000000 - (0xE85250D6 + (mb2_end - mb2_start))
    align 8
    dw 0
    dw 0
    dd 8
mb2_end:

section .bss
align 4096
pml4_table: resb 4096
pdp_table:  resb 4096
pd_table:   resb 4096

align 16
stack_bottom:
    resb 16384
stack_top:

section .rodata
gdt64:
    dq 0
.code:
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)
gdt64_pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .text
global _start
extern kernel_main

_start:
    mov edi, eax            ; save mb2 magic
    mov esi, ebx            ; save mb2 info pointer
    mov esp, stack_top

    ; Check CPUID support
    pushfd
    pop eax
    mov ecx, eax
    xor eax, (1<<21)
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .error

    ; Check long mode support
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .error
    mov eax, 0x80000001
    cpuid
    test edx, (1<<29)
    jz .error

    ; ---- Set up page tables ----
    ; PML4[0] → PDP
    mov eax, pdp_table
    or  eax, 0b11
    mov [pml4_table], eax

    ; PDP[0] → PD
    mov eax, pd_table
    or  eax, 0b11
    mov [pdp_table], eax

    ; Map 512 x 2MB huge pages = 1GB
    ; This covers 0x0 to 0x40000000 — enough for kernel + bitmap + everything
    mov ecx, 0
.map_pd:
    cmp ecx, 512
    jge .map_pd_done
    mov eax, ecx
    shl eax, 21             ; eax = ecx * 2MB physical address
    or  eax, 0b10000011     ; present + writable + huge page
    mov [pd_table + ecx*8], eax
    inc ecx
    jmp .map_pd
.map_pd_done:

    ; Enable PAE
    mov eax, cr4
    or  eax, (1<<5)
    mov cr4, eax

    ; Load PML4 into CR3
    mov eax, pml4_table
    mov cr3, eax

    ; Enable long mode in EFER MSR
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1<<8)
    wrmsr

    ; Enable paging + protected mode
    mov eax, cr0
    or  eax, (1<<31) | (1<<0)
    mov cr0, eax

    ; Far jump to 64-bit
    lgdt [gdt64_pointer]
    jmp 0x08:.long_mode

bits 64
.long_mode:
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; edi = mb2 magic, esi = mb2 info (set above, ABI-compatible)
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

bits 32
.error:
    mov dword [0xB8000], 0x4F524F45   ; 'ER'
    mov dword [0xB8004], 0x4F214F52   ; 'R!'
    cli
    hlt
