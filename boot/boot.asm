; =============================================================================
; boot/boot.asm  -  Stage 1 boot sector
;
; Loads the 8 KiB stage-2 bootloader from LBA 1..16 to 0000:8000 and jumps to
; it.  Keeping the boot sector tiny leaves room for the graphics/boot-menu
; logic in stage 2.
; =============================================================================

[BITS 16]
[ORG 0x7C00]

STAGE2_LBA      equ 1
STAGE2_SECTORS  equ 16
STAGE2_ADDR     equ 0x8000
SECTORS_TRACK   equ 18
HEADS           equ 2

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl
    mov si, msg_load
    call puts

    mov word [cur_lba], STAGE2_LBA
    mov word [remaining], STAGE2_SECTORS
    mov word [buf], STAGE2_ADDR

.read_loop:
    cmp word [remaining], 0
    je .loaded

    mov ax, [cur_lba]
    xor dx, dx
    mov bx, SECTORS_TRACK
    div bx
    inc dx
    mov cl, dl

    xor dx, dx
    mov bx, HEADS
    div bx
    mov ch, al
    mov dh, dl
    mov dl, [boot_drive]

    xor ax, ax
    mov es, ax
    mov bx, [buf]
    mov ax, 0x0201
    int 0x13
    jc .error

    add word [buf], 512
    inc word [cur_lba]
    dec word [remaining]
    jmp .read_loop

.error:
    mov si, msg_err
    call puts
    cli
    hlt
    jmp $

.loaded:
    mov si, msg_ok
    call puts
    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_ADDR

puts:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .loop
.done:
    popa
    ret

boot_drive: db 0
cur_lba:    dw 0
remaining:  dw 0
buf:        dw 0
msg_load:   db "Altitude OS  |  stage 1", 13, 10, 0
msg_ok:     db "Loading stage 2...", 13, 10, 0
msg_err:    db "Stage 2 read error - halted.", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
