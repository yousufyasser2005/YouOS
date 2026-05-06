; =============================================================================
; YouOS - Context Switch
; =============================================================================

bits 64
global context_switch

; void context_switch(cpu_context_t* old, cpu_context_t* new)
; rdi = old context, rsi = new context

context_switch:
    ; ---- Save old context ----
    mov [rdi + 0],   r15
    mov [rdi + 8],   r14
    mov [rdi + 16],  r13
    mov [rdi + 24],  r12
    mov [rdi + 32],  r11
    mov [rdi + 40],  r10
    mov [rdi + 48],  r9
    mov [rdi + 56],  r8
    mov [rdi + 64],  rbp
    mov [rdi + 72],  rdi
    mov [rdi + 80],  rsi
    mov [rdi + 88],  rdx
    mov [rdi + 96],  rcx
    mov [rdi + 104], rbx
    mov [rdi + 112], rax

    ; Save rsp (points to return address right now)
    mov rax, rsp
    mov [rdi + 144], rax

    ; Save return address as rip
    mov rax, [rsp]
    mov [rdi + 120], rax

    ; Save rflags
    pushfq
    pop rax
    mov [rdi + 136], rax

    ; ---- Load new context ----
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov r11, [rsi + 32]
    mov r10, [rsi + 40]
    mov r9,  [rsi + 48]
    mov r8,  [rsi + 56]
    mov rbp, [rsi + 64]
    mov rdx, [rsi + 88]
    mov rcx, [rsi + 96]
    mov rbx, [rsi + 104]
    mov rax, [rsi + 112]

    ; Restore rflags
    push qword [rsi + 136]
    popfq

    ; Check if this is a fresh process (rip == rsp[0])
    ; For fresh processes, rsp points just above the entry address
    ; We restore rsp then ret to jump to entry
    mov rsp, [rsi + 144]

    ; Jump to new process rip
    push qword [rsi + 120]
    ret
