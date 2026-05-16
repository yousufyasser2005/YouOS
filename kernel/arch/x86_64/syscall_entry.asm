bits 64
global syscall_entry
extern syscall_handler
extern kernel_stack_top

syscall_entry:
    cli
    mov [user_rsp_tmp], rsp
    mov rsp, [kernel_stack_top]

    ; Push all registers. Stack layout after pushes (rsp+0=r15 ... rsp+112=rcx):
    push rcx        ; [rsp+112] user RIP
    push r11        ; [rsp+104] user RFLAGS
    push rax        ; [rsp+96]  syscall number
    push rdi        ; [rsp+88]  a1
    push rsi        ; [rsp+80]  a2
    push rdx        ; [rsp+72]  a3
    push r10        ; [rsp+64]  a4
    push r8         ; [rsp+56]  a5
    push r9         ; [rsp+48]  a6
    push rbx        ; [rsp+40]
    push rbp        ; [rsp+32]
    push r12        ; [rsp+24]
    push r13        ; [rsp+16]
    push r14        ; [rsp+8]
    push r15        ; [rsp+0]

    ; Set up arguments for syscall_handler(num, a1, a2, a3, a4, a5)
    mov rdi, [rsp+96]    ; num
    mov rsi, [rsp+88]    ; a1
    mov rdx, [rsp+80]    ; a2
    mov rcx, [rsp+72]    ; a3  (ok to clobber rcx here, restored from stack after call)
    mov r8,  [rsp+56]    ; a4
    mov r9,  [rsp+48]    ; a5
    call syscall_handler
    ; rax = return value — DO NOT touch rax after this

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    add rsp, 8      ; skip saved rax (syscall number) — keep rax=return value
    pop r11         ; user RFLAGS
    pop rcx         ; user RIP

    mov rsp, [user_rsp_tmp]
    sti
    db 0x48, 0x0F, 0x07    ; sysretq

section .bss
user_rsp_tmp: resq 1
