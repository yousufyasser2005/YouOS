bits 64
global _start
extern main

section .text._start
_start:
    xor rbp, rbp        ; mark end of stack frames
    call main
    ; main returned — call sys_exit with return value
    mov rdi, rax
    mov rax, 0          ; SYS_EXIT
    syscall
    ; should never reach here
.hang:
    hlt
    jmp .hang
