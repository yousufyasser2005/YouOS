bits 64

global switch_context
global process_trampoline
extern process_exit

; switch_context(uint64_t* old_rsp_ptr [rdi], uint64_t new_rsp [rsi])
switch_context:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov  [rdi], rsp     ; save old process RSP
    mov  rsp,   rsi     ; load new process RSP
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp
    ret                 ; jumps to new process's saved return address

process_trampoline:
    sti                 ; re-enable interrupts
    call rbx            ; rbx = entry function (restored by switch_context)
    call process_exit   ; entry returned — clean up
.hang:
    hlt
    jmp .hang
