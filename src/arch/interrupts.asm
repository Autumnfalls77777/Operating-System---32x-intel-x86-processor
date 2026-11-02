; =============================================================================
; src/arch/interrupts.asm  -  IDT stubs and common interrupt frame
;
; Every vector pushes a uniform frame:
;     vector, error, eax, ecx, edx, ebx, esp, ebp, esi, edi, gs, fs, es, ds
; The C dispatcher receives a pointer to the frame (regs_t).
; =============================================================================

[BITS 32]
[SECTION .text]

[EXTERN isr_dispatch]

%macro ISR_NOERR 1
[GLOBAL isr%1]
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
[GLOBAL isr%1]
isr%1:
    push dword %1
    jmp isr_common
%endmacro

; ── CPU exceptions ──────────────────────────────────────────────────────────
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; ── Hardware IRQs 0..15 (vectors 32..47) ───────────────────────────────────
ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47

; ── System call gate ────────────────────────────────────────────────────────
ISR_NOERR 128

; ── Default stub for all other vectors ─────────────────────────────────────
[GLOBAL isr_default]
isr_default:
    push dword 0
    push dword 255
    jmp isr_common

; ── Common frame ───────────────────────────────────────────────────────────
isr_common:
    pushad

    mov ax, ds
    push eax
    mov ax, es
    push eax
    mov ax, fs
    push eax
    mov ax, gs
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call isr_dispatch
    add esp, 4

    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax

    popad
    add esp, 8          ; discard vector + error code
    iret

; ── Stub address table (256 entries) ───────────────────────────────────────
[SECTION .data]
[GLOBAL isr_stub_table]
isr_stub_table:
%assign i 0
%rep 256
    %if i < 48 || i == 128
        dd isr %+ i
    %else
        dd isr_default
    %endif
%assign i i+1
%endrep
