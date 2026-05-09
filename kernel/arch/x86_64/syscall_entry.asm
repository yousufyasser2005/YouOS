; =============================================================================
; YouOS - Syscall Entry Point
;
; The SYSCALL instruction jumps here from userspace.
; CPU state on entry:
;   - RCX = user RIP (return address)
;   - R11 = user RFLAGS
;   - RSP = still user stack (we must switch to kernel stack)
;   - CS  = kernel code segment (already switched by CPU)
;
; We must:
;   1. Switch to kernel stack
;   2. Save user registers
;   3. Call C handler
;   4. Restore user registers
;   5. SYSRET back to userspace
; =============================================================================

bits 64

global syscall_entry
extern syscall_handler

; Per-CPU kernel stack pointer stored in GS base (we use a simple global)
extern kernel_stack_top

syscall_entry:
    ; Swap to kernel stack using a temporary scratch area
    ; Save user RSP in a temporary location
    mov [user_rsp_tmp], rsp

    ; Switch to kernel stack
    mov rsp, [kernel_stack_top]

    ; Now we're on the kernel stack — save everything
    push rcx            ; user RIP
    push r11            ; user RFLAGS
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push rax            ; syscall number

    ; Push user RSP
    push qword [user_rsp_tmp]

    ; Call C handler:
    ; syscall_handler(uint64_t syscall_no, uint64_t arg1, arg2, arg3, arg4, arg5)
    ; Args are already in rdi, rsi, rdx, r10, r8, r9 from userspace
    ; syscall number is in rax
    mov rdi, rax        ; syscall number → first arg
    ; rsi, rdx, r10, r8, r9 already set by userspace

    call syscall_handler

    ; rax now holds return value

    ; Restore user RSP
    pop rcx             ; was user RSP
    mov [user_rsp_tmp], rcx

    ; Restore saved registers
    pop rcx             ; was rax (syscall no) - discard
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop r11             ; user RFLAGS
    pop rcx             ; user RIP

    ; Restore user stack
    mov rsp, [user_rsp_tmp]

    ; Return to userspace
    sysretq

section .bss
user_rsp_tmp: resq 1
