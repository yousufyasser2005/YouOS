; =============================================================================
; YouOS - Jump to Userspace (Ring 3)
;
; jump_to_userspace(uint64_t entry, uint64_t user_stack)
;   rdi = entry point (Ring 3 RIP)
;   rsi = user stack pointer
;
; We use IRETQ to switch to Ring 3:
;   Push SS, RSP, RFLAGS, CS, RIP onto kernel stack
;   Then IRETQ pops them and switches privilege level
; =============================================================================

bits 64
global jump_to_userspace

; GDT selectors
; User code = 0x18 | 3 = 0x1B (Ring 3)
; User data = 0x20 | 3 = 0x23 (Ring 3)
USER_CODE equ 0x1B
USER_DATA equ 0x23

jump_to_userspace:
    ; rdi = entry point
    ; rsi = user stack

    cli

    ; Set up data segments for Ring 3
    mov ax, USER_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Build IRETQ frame on kernel stack:
    ;   [SS      ]
    ;   [RSP     ]
    ;   [RFLAGS  ]
    ;   [CS      ]
    ;   [RIP     ]
    push USER_DATA      ; SS
    push rsi            ; RSP (user stack)
    pushfq              ; RFLAGS
    or qword [rsp], (1 << 9)  ; set IF (enable interrupts in userspace)
    push USER_CODE      ; CS
    push rdi            ; RIP (entry point)

    iretq               ; switch to Ring 3!
