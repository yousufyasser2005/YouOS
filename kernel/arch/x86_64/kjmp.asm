bits 64
global ksetjmp
global klongjmp

; int ksetjmp(kjmp_buf_t* buf)  -- rdi = buf
ksetjmp:
    lea rax, [rsp+8]    ; RSP as it will be after ksetjmp returns (past return addr)
    mov [rdi+0],  rax   ; save that as the restore RSP
    mov [rdi+8],  rbp
    mov [rdi+16], rbx
    mov [rdi+24], r12
    mov [rdi+32], r13
    mov [rdi+40], r14
    mov [rdi+48], r15
    mov rax, [rsp]      ; return address = rip to resume at
    mov [rdi+56], rax
    xor eax, eax        ; return 0
    ret

; void klongjmp(kjmp_buf_t* buf)  -- rdi = buf
klongjmp:
    mov rsp, [rdi+0]    ; restore RSP (already past return addr)
    mov rbp, [rdi+8]
    mov rbx, [rdi+16]
    mov r12, [rdi+24]
    mov r13, [rdi+32]
    mov r14, [rdi+40]
    mov r15, [rdi+48]
    mov rax, 1          ; return value = 1
    jmp [rdi+56]        ; jump to saved rip
