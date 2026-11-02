; =============================================================================
; boot/stage2.asm  -  Stage 2: kernel loader, E820 map, VBE setup, PM entry
;
; Loaded at 0000:8000 by the boot sector.  Responsibilities:
;   1. Enable A20 and load the kernel image to 0x00010000
;   2. Collect the BIOS E820 memory map
;   3. Present a display-resolution menu and program VBE linear framebuffer
;   4. Write boot_info_t at 0x5000
;   5. Enter 32-bit protected mode and jump to the kernel
; =============================================================================

[BITS 16]
[ORG 0x8000]

KERNEL_LOAD_ADDR  equ 0x10000
KERNEL_LBA        equ 17       ; 1 (boot) + 16 (stage 2)
KERNEL_SECTORS    equ 896      ; 448 KiB max kernel payload
SECTORS_TRACK     equ 18
HEADS             equ 2

; boot_info_t field addresses (keep in sync with include/bootinfo.h)
BI_MAGIC          equ 0x5000
BI_FB             equ 0x5004
BI_WIDTH          equ 0x5008
BI_HEIGHT         equ 0x500C
BI_PITCH          equ 0x5010
BI_BPP            equ 0x5014
BI_E820_COUNT     equ 0x5018
BI_MEM_LOWER      equ 0x501C
BI_MEM_UPPER      equ 0x5020
BI_BOOT_DRIVE     equ 0x5024
BI_E820           equ 0x5028

VBE_CTRL          equ 0x6000
VBE_MODE          equ 0x6200
E820_BUF          equ 0x6400

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl
    call enable_a20

    mov si, msg_banner
    call puts

    call load_kernel
    call collect_e820
    call collect_meminfo
    call select_resolution
    call setup_vbe

    mov si, msg_pm
    call puts

    ; â”€â”€ Enter protected mode â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:pm_entry

; â”€â”€ A20 gate â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
enable_a20:
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al
    ret

; â”€â”€ Read the kernel from disk into 0x10000 â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
load_kernel:
    mov si, msg_kernel
    call puts
    mov word [cur_lba], KERNEL_LBA
    mov word [remaining], KERNEL_SECTORS
    mov dword [buf_ptr], KERNEL_LOAD_ADDR

.load_loop:
    cmp word [remaining], 0
    je .done

    ; LBA -> CHS
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

    ; Linear destination -> segment:offset (never crosses a 64 KiB boundary)
    mov ax, [buf_ptr]
    and ax, 0x000F
    mov bx, ax
    mov eax, [buf_ptr]
    shr eax, 4
    mov es, ax

    mov ax, 0x0201
    int 0x13
    jc .error

    add dword [buf_ptr], 512
    inc word [cur_lba]
    dec word [remaining]
    jmp .load_loop

.error:
    mov si, msg_disk_err
    call puts
    cli
    hlt
    jmp $

.done:
    xor ax, ax
    mov es, ax
    mov si, msg_ok
    call puts
    ret

; â”€â”€ BIOS E820 memory map â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
collect_e820:
    mov word [e820_cnt], 0
    mov word [e820_dst], BI_E820
    xor ebx, ebx
.loop:
    xor ax, ax
    mov es, ax
    mov ds, ax
    mov di, E820_BUF
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done

    mov si, [e820_dst]
    mov eax, [E820_BUF + 0]
    mov [si + 0], eax
    mov eax, [E820_BUF + 8]
    mov [si + 4], eax
    mov eax, [E820_BUF + 16]
    mov [si + 8], eax

    add word [e820_dst], 12
    inc word [e820_cnt]
    cmp word [e820_cnt], 16
    jae .done
    test ebx, ebx
    jz .done
    jmp .loop

.done:
    xor ax, ax
    mov ds, ax
    mov es, ax
    movzx eax, word [e820_cnt]
    mov [BI_E820_COUNT], eax
    ret

; â”€â”€ Conventional + extended memory sizes â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
collect_meminfo:
    int 0x12
    movzx eax, ax
    mov [BI_MEM_LOWER], eax

    mov ax, 0xE801
    int 0x15
    jc .no_e801
    movzx eax, ax
    movzx ebx, bx
    shl ebx, 6
    add eax, ebx
    mov [BI_MEM_UPPER], eax
    ret
.no_e801:
    mov dword [BI_MEM_UPPER], 0
    ret

; â”€â”€ Display resolution menu â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
select_resolution:
    mov si, msg_menu
    call puts

    ; Wait roughly two seconds using the BIOS tick counter at 0040:006C,
    ; polling the keyboard so an early keypress is not lost.  (INT 15h/AH=86h
    ; is not reliable on every BIOS, so it is deliberately avoided here.)
    mov eax, [0x046C]
    add eax, 36                 ; ~18.2 ticks per second
    mov [wait_ticks], eax

.wait:
    mov ah, 0x01
    int 0x16
    jnz .have_key
    mov eax, [0x046C]
    cmp eax, [wait_ticks]
    jb .wait
    jmp .default

.have_key:
    mov ah, 0x00
    int 0x16
    cmp al, '1'
    je .r1
    cmp al, '2'
    je .r2
    cmp al, '3'
    je .r3
    cmp al, '4'
    je .r4
    jmp .default

.r1: mov word [want_w], 1024
     mov word [want_h], 768
     jmp .print
.r2: mov word [want_w], 1280
     mov word [want_h], 720
     jmp .print
.r3: mov word [want_w], 1280
     mov word [want_h], 1024
     jmp .print
.r4: mov word [want_w], 1920
     mov word [want_h], 1080
     jmp .print

.default:
    mov word [want_w], 1024
    mov word [want_h], 768

.print:
    mov si, msg_chosen
    call puts
    mov ax, [want_w]
    call put_dec
    mov al, 'x'
    call putc
    mov ax, [want_h]
    call put_dec
    call newline
    ret

; â”€â”€ VBE mode enumeration and mode set â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
setup_vbe:
    mov si, msg_vbe
    call puts

    xor ax, ax
    mov es, ax
    mov di, VBE_CTRL
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .fail
    cmp dword [VBE_CTRL], 0x41534556      ; "VESA"
    jne .fail

    mov si, [VBE_CTRL + 14]
    mov ax, [VBE_CTRL + 16]
    mov [mode_off], si
    mov [mode_seg], ax
    mov word [candidate_mode], 0

.mode_loop:
    mov ax, [mode_seg]
    mov fs, ax
    mov si, [mode_off]
    mov cx, [fs:si]
    cmp cx, 0xFFFF
    je .end_of_list
    add si, 2
    mov [mode_off], si

    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov di, VBE_MODE
    int 0x10
    cmp ax, 0x004F
    jne .mode_loop

    mov ax, [VBE_MODE + 0]
    test ax, 0x0001              ; mode supported
    jz .mode_loop
    test ax, 0x0080              ; linear framebuffer available
    jz .mode_loop

    mov ax, [VBE_MODE + 18]
    cmp ax, [want_w]
    jne .mode_loop
    mov ax, [VBE_MODE + 20]
    cmp ax, [want_h]
    jne .mode_loop

    mov al, [VBE_MODE + 25]
    cmp al, 32
    je .found32
    cmp al, 24
    je .found24
    jmp .mode_loop

.found32:
    mov [found_mode], cx
    mov byte [found_bpp], 32
    jmp .end_of_list

.found24:
    ; remember a 24bpp fallback but keep looking for a 32bpp mode
    mov [candidate_mode], cx
    mov byte [candidate_bpp], 24
    jmp .mode_loop

.end_of_list:
    cmp word [found_mode], 0
    jne .have_mode
    cmp word [candidate_mode], 0
    je .fail
    mov ax, [candidate_mode]
    mov [found_mode], ax
    mov al, [candidate_bpp]
    mov [found_bpp], al

.have_mode:
    ; Print the chosen mode while BIOS text output is still available.
    mov si, msg_mode
    call puts
    mov ax, [want_w]
    call put_dec
    mov al, 'x'
    call putc
    mov ax, [want_h]
    call put_dec
    mov si, msg_mode2
    call puts
    movzx ax, byte [found_bpp]
    call put_dec
    call newline

    ; Set the mode with the linear framebuffer flag.
    xor ax, ax
    mov es, ax
    mov ax, 0x4F02
    mov bx, [found_mode]
    or bx, 0x4000
    int 0x10
    cmp ax, 0x004F
    jne .fail

    ; Re-query to obtain the authoritative pitch / framebuffer address.
    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, [found_mode]
    mov di, VBE_MODE
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov eax, [VBE_MODE + 40]      ; PhysBasePtr
    test eax, eax
    jz .fail
    mov [BI_FB], eax

    movzx eax, word [VBE_MODE + 18]
    mov [BI_WIDTH], eax
    movzx eax, word [VBE_MODE + 20]
    mov [BI_HEIGHT], eax
    movzx eax, word [VBE_MODE + 16]
    mov [BI_PITCH], eax
    movzx eax, byte [VBE_MODE + 25]
    mov [BI_BPP], eax

    mov dword [BI_MAGIC], 0x4E4F5641
    movzx eax, byte [boot_drive]
    mov [BI_BOOT_DRIVE], eax
    ret

.fail:
    mov si, msg_vbe_fail
    call puts
    cli
    hlt
    jmp $

; â”€â”€ BIOS text helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
putc:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    ret

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

newline:
    mov al, 13
    call putc
    mov al, 10
    call putc
    ret

put_dec:
    pusha
    mov cx, 0
    mov bx, 10
.digits:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .digits
.print:
    pop ax
    add al, '0'
    call putc
    loop .print
    popa
    ret

; â”€â”€ 32-bit entry stub â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
[BITS 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000
    mov ebp, esp
    mov eax, KERNEL_LOAD_ADDR
    jmp eax

; â”€â”€ GDT â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
[BITS 16]
gdt_start:
    dq 0x0000000000000000
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; â”€â”€ Data â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
boot_drive:      db 0
cur_lba:         dw 0
remaining:       dw 0
buf_ptr:         dd 0
want_w:          dw 1024
want_h:          dw 768
wait_ticks:      dd 0
found_mode:      dw 0
found_bpp:       db 0
candidate_mode:  dw 0
candidate_bpp:   db 0
mode_seg:        dw 0
mode_off:        dw 0
e820_cnt:        dw 0
e820_dst:        dw 0

msg_banner:    db "Altitude OS  |  stage 2 bootloader", 13, 10, 0
msg_kernel:    db "Loading kernel image...", 13, 10, 0
msg_ok:        db "Kernel loaded at 0x10000.", 13, 10, 0
msg_disk_err:  db "Kernel read error - halted.", 13, 10, 0
msg_menu:      db 13, 10, "Select display: [1] 1024x768  [2] 1280x720  [3] 1280x1024  [4] 1920x1080", 13, 10
               db "Press 1-4 within 2 seconds (default 1)...", 13, 10, 0
msg_chosen:    db "Display request: ", 0
msg_vbe:       db "Programming VBE linear framebuffer...", 13, 10, 0
msg_mode:      db "Mode: ", 0
msg_mode2:     db " @ ", 0
msg_vbe_fail:  db "VBE graphics mode unavailable - halted.", 13, 10, 0
msg_pm:        db "Entering 32-bit protected mode...", 13, 10, 0

; Pad stage 2 to exactly 16 sectors so the kernel starts at LBA 17.
times (16 * 512) - ($ - $$) db 0
