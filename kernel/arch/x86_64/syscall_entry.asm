bits 64
default rel     ; use RIP-relative addressing for label references below
                ; (e.g. [user_rsp_tmp], [kernel_stack_top]) instead of
                ; implicit absolute addressing, which newer NASM versions
                ; warn about as deprecated
global syscall_entry
extern syscall_handler
extern kernel_stack_top

syscall_entry:
    cli
    ; user_rsp_tmp is a scratch (register-free) holding spot ONLY for the
    ; few instructions until we have a valid kernel stack to push onto --
    ; it must NOT be relied on to still hold OUR value once we're past
    ; that point, because if this syscall blocks/yields (e.g. sys_exec()
    ; waiting on a child), a DIFFERENT process can make its own syscalls
    ; (each overwriting this same global) before we ever get back here to
    ; read it. That was a real, previously-latent bug: the blocked parent
    ; would resume and SYSRET with the *child's* last-saved user RSP
    ; instead of its own, corrupting the parent's ring-3 stack pointer.
    ; Fix: push the saved value onto THIS process's own kernel stack
    ; immediately (making it part of the same per-invocation, per-process
    ; state as every other saved register below) and pop it back
    ; symmetrically on exit, instead of leaving it sitting in a global for
    ; the whole, possibly-long, possibly-preempted-by-another-process
    ; duration of the syscall.
    mov [user_rsp_tmp], rsp
    mov rsp, [kernel_stack_top]

    ; Push all registers. Stack layout after pushes (rsp+0=r15 ... rsp+112=rcx,
    ; rsp+120=saved user RSP):
    push qword [user_rsp_tmp]  ; [rsp+120] user RSP (see note above)
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
    mov r8,  [rsp+64]    ; a4 (from r10 slot — user passes a4 in r10)
    mov r9,  [rsp+56]    ; a5 (from r8 slot  — user passes a5 in r8)
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
    pop rsp         ; user RSP — popped from THIS process's own kernel
                    ; stack, not the shared scratch global (see note above)

    sti
    db 0x48, 0x0F, 0x07    ; sysretq

section .data
global user_rsp_tmp
user_rsp_tmp: dq 0
