; =============================================================================
; src/arch/kernel_entry.asm  -  32-bit kernel entry
;
; The stage-2 bootloader jumps to physical 0x10000 with a flat 4 GiB address
; space.  We install a known-good stack, zero the .bss section (the flat binary
; does not contain it), and call kernel_main(boot_info_t *).
; =============================================================================

[BITS 32]
[SECTION .text.entry]
[GLOBAL start]
[EXTERN kernel_main]
[EXTERN __bss_start]
[EXTERN __bss_end]

start:
    cli
    cld

    ; ── Stack ───────────────────────────────────────────────────────────────
    mov esp, 0x9F000
    mov ebp, esp

    ; ── Zero .bss ───────────────────────────────────────────────────────────
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    ; ── Boot information pointer ────────────────────────────────────────────
    mov eax, 0x5000
    push eax
    call kernel_main
    add esp, 4

.hang:
    cli
    hlt
    jmp .hang
