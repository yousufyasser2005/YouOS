; =============================================================================
; YouOS - Kernel Entry Point
; Multiboot2 entry (32-bit) → Long Mode (64-bit) → kernel_main
; =============================================================================

bits 32

; =============================================================================
; Multiboot2 Header
; =============================================================================
section .multiboot2
align 8
mb2_start:
    dd 0xE85250D6                                    ; magic
    dd 0                                             ; arch: i386
    dd mb2_end - mb2_start                           ; length
    dd 0x100000000 - (0xE85250D6 + (mb2_end - mb2_start)) ; checksum
    ; end tag
    align 8
    dw 0
    dw 0
    dd 8
mb2_end:

; =============================================================================
; BSS - Stack + Page Tables
; =============================================================================
section .bss
align 4096

pml4_table:  resb 4096
pdp_table:   resb 4096
pd_table:    resb 4096

align 16
stack_bottom:
    resb 16384          ; 16KB stack
stack_top:

; =============================================================================
; Read-Only Data - GDT
; =============================================================================
section .rodata
gdt64:
    dq 0                        ; null descriptor
.code:
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)   ; code segment
gdt64_pointer:
    dw $ - gdt64 - 1            ; limit
    dq gdt64                    ; base

; =============================================================================
; Text - Entry Point
; =============================================================================
section .text
global _start
extern kernel_main

_start:
    ; Save multiboot2 registers before we touch them
    mov edi, eax                ; mb2 magic  → edi (1st arg in 64-bit SysV ABI)
    mov esi, ebx                ; mb2 info   → esi (2nd arg)

    ; Set up stack
    mov esp, stack_top

    ; ---- Step 1: Check for CPUID support ----
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
    je .no_cpuid

    ; ---- Step 2: Check for Long Mode via CPUID ----
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, (1<<29)
    jz .no_long_mode

    ; ---- Step 3: Set up Page Tables (identity map first 2MB) ----
    ; PML4[0] → PDP
    mov eax, pdp_table
    or  eax, 0b11               ; present + writable
    mov [pml4_table], eax

    ; PDP[0] → PD
    mov eax, pd_table
    or  eax, 0b11
    mov [pdp_table], eax

    ; PD[0] → 2MB huge page at 0x0
    mov dword [pd_table], 0b10000011    ; present + writable + huge

    ; ---- Step 4: Enable PAE ----
    mov eax, cr4
    or  eax, (1<<5)             ; PAE bit
    mov cr4, eax

    ; ---- Step 5: Load PML4 into CR3 ----
    mov eax, pml4_table
    mov cr3, eax

    ; ---- Step 6: Enable Long Mode in EFER ----
    mov ecx, 0xC0000080         ; EFER MSR
    rdmsr
    or  eax, (1<<8)             ; LME bit
    wrmsr

    ; ---- Step 7: Enable Paging (activates long mode) ----
    mov eax, cr0
    or  eax, (1<<31) | (1<<0)  ; PG + PE
    mov cr0, eax

    ; ---- Step 8: Far jump to 64-bit code segment ----
    lgdt [gdt64_pointer]
    jmp 0x08:.long_mode_start

; -----------------------------------------------------------------------------
bits 64
.long_mode_start:
    ; Zero out all segment registers
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; edi = mb2 magic, esi = mb2 info (already set above, preserved through)
    ; Call kernel_main(uint32_t magic, uint32_t info_addr)
    call kernel_main

    ; Hang if kernel_main ever returns
.hang:
    cli
    hlt
    jmp .hang

; -----------------------------------------------------------------------------
bits 32
.no_cpuid:
.no_long_mode:
    ; Print 'ERR' to VGA and halt (we're still in 32-bit here)
    mov dword [0xB8000], 0x4F524F45   ; 'ER' red on white
    mov dword [0xB8004], 0x4F214F52   ; 'R!' red on white
    cli
    hlt
