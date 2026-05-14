bits 64
global syscall_entry
extern syscall_handler
extern kernel_stack_top

syscall_entry:
    cli
    mov [user_rsp_tmp], rsp
    mov rsp, [kernel_stack_top]

    push rcx        ; user RIP  (must survive to sysretq)
    push r11        ; user RFLAGS
    push rax        ; syscall number
    push rdi        ; a1
    push rsi        ; a2
    push rdx        ; a3
    push r10        ; a4
    push r8         ; a5
    push r9         ; a6
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    ; Stack layout (rsp+0=r15 ... rsp+112=rcx)
    ; Pass args WITHOUT touching rcx before the call
    ; Use r11 as scratch (already saved)
    mov  r11, [rsp+96]   ; num  -> rdi
    mov  rdi, r11
    mov  rsi, [rsp+88]   ; a1
    mov  rdx, [rsp+80]   ; a2
    mov  r10, [rsp+72]   ; a3 -> use r10 (4th arg in Linux ABI via r10)
    ; For System V AMD64, 4th arg is rcx but we can't use rcx
    ; gcc syscall_handler takes (num,a1,a2,a3,a4,a5) — 4th arg goes in rcx
    ; BUT we need rcx for sysretq. Solution: shadow copy via stack
    sub  rsp, 8
    mov  [rsp], r10      ; push a3 as stack arg? No — use different reg
    add  rsp, 8
    ; Actually just set rcx=a3 — it'll be clobbered by call anyway
    ; and we restore rcx from our saved copy on the stack after the call
    mov  rcx, [rsp+72]   ; a3 (rcx will be clobbered by call, that's ok)
    mov  r8,  [rsp+64]   ; a4  (was r10 slot... wait let me recount)

    ; Recount from rsp after 15 pushes:
    ; rsp+0:r15 rsp+8:r14 rsp+16:r13 rsp+24:r12 rsp+32:rbp rsp+40:rbx
    ; rsp+48:r9 rsp+56:r8 rsp+64:r10 rsp+72:rdx rsp+80:rsi rsp+88:rdi
    ; rsp+96:rax rsp+104:r11 rsp+112:rcx
    mov rdi, [rsp+96]    ; num
    mov rsi, [rsp+88]    ; a1
    mov rdx, [rsp+80]    ; a2
    mov rcx, [rsp+72]    ; a3  (ok to clobber rcx here, restored from stack)
    mov r8,  [rsp+56]    ; a4  (was r8)
    mov r9,  [rsp+48]    ; a5  (was r9)
    call syscall_handler
    ; rax = return value, rcx is clobbered by call (contains ret addr) - doesn't matter

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
    pop rax             ; discard saved syscall number
    pop r11             ; restore user RFLAGS
    pop rcx             ; restore user RIP

    mov rsp, [user_rsp_tmp]
    sti
    db 0x48, 0x0F, 0x07    ; sysretq opcode explicitly

section .bss
user_rsp_tmp: resq 1
