# =============================================================================
# Makefile  -  Altitude OS 1.0.0 "Aurora"
#
#   mingw32-make all            build build/os_image.bin
#   mingw32-make run            boot the OS in QEMU (windowed)
#   mingw32-make run-scaled     boot with the window scaled to fit
#   mingw32-make run-fullscreen boot straight into fullscreen
#   mingw32-make debug          boot paused with a GDB stub
#   mingw32-make clean          remove build products
#   mingw32-make font           regenerate include/font8x16.h
#
#   1.44 MiB floppy layout:
#     LBA 0        boot sector      (512 bytes)
#     LBA 1..16    stage 2          (8 KiB)
#     LBA 17..     kernel           (up to 896 sectors / 448 KiB)
# =============================================================================

ASM       := C:/msys64_new/ucrt64/bin/nasm.exe
CC        := C:/msys64_new/ucrt64/bin/gcc.exe
LD        := C:/msys64_new/ucrt64/bin/ld.exe
OBJCOPY   := C:/msys64_new/ucrt64/bin/objcopy.exe
QEMU      := C:/msys64_new/ucrt64/bin/qemu-system-x86_64.exe
POWERSHELL:= powershell.exe

# Use the native command interpreter so PowerShell variables survive recipes.
SHELL := cmd.exe
.SHELLFLAGS := /c

BUILD_DIR := build
IMAGE     := $(BUILD_DIR)/os_image.bin
KERNEL_MAX:= 458752
IMAGE_SIZE:= 1474560

CFLAGS := -m32 -std=gnu11 -O2 \
          -ffreestanding -fno-builtin -fno-leading-underscore \
          -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables \
          -fno-omit-frame-pointer -mno-stack-arg-probe -nostdlib \
          -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
          -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
          -Wno-missing-field-initializers \
          -Iinclude -Isrc/apps -c

LDFLAGS := -m i386pe --image-base 0x0 --file-alignment 0x10 \
           --section-alignment 0x10 --no-insert-timestamp \
           -T src/linker.ld

QEMU_BASE := -m 256M -cpu qemu32 -vga std -rtc base=localtime \
             -name "Altitude OS" \
             -drive format=raw,file=$(IMAGE),if=floppy -boot order=a \
             -serial file:$(BUILD_DIR)/serial.log

# â”€â”€ Sources â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

C_SOURCES := $(wildcard src/kernel/*.c) \
             $(wildcard src/arch/*.c) \
             $(wildcard src/drivers/*.c) \
             $(wildcard src/lib/*.c) \
             $(wildcard src/mm/*.c) \
             $(wildcard src/fs/*.c) \
             $(wildcard src/proc/*.c) \
             $(wildcard src/gui/*.c) \
             $(wildcard src/apps/*.c)

ASM_SOURCES := src/arch/kernel_entry.asm \
               src/arch/interrupts.asm

OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES)) \
           $(patsubst src/%.asm,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

BUILD_DIRS := $(sort $(dir $(OBJECTS))) $(BUILD_DIR)

.PHONY: all run run-native run-scaled run-fullscreen debug clean font size help

all: $(IMAGE)

$(BUILD_DIRS):
	@$(POWERSHELL) -NoProfile -Command "New-Item -ItemType Directory -Path '$@' -Force | Out-Null"

# â”€â”€ Compile â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

$(BUILD_DIR)/%.o: src/%.c Makefile | $(BUILD_DIRS)
	@echo   CC    $<
	@$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: src/%.asm Makefile | $(BUILD_DIRS)
	@echo   ASM   $<
	@$(ASM) -f win32 $< -o $@

# â”€â”€ Bootloader â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

$(BUILD_DIR)/boot.bin: boot/boot.asm | $(BUILD_DIRS)
	@echo   BOOT  $<
	@$(ASM) -f bin $< -o $@

$(BUILD_DIR)/stage2.bin: boot/stage2.asm | $(BUILD_DIRS)
	@echo   BOOT  $<
	@$(ASM) -f bin $< -o $@
	@$(POWERSHELL) -NoProfile -Command "$$l=(Get-Item '$(BUILD_DIR)/stage2.bin').Length; if ($$l -ne 8192) { Write-Host 'stage2 must be 8192 bytes'; exit 1 }"

# â”€â”€ Kernel â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

$(BUILD_DIR)/kernel.elf: $(OBJECTS) src/linker.ld | $(BUILD_DIRS)
	@echo   LD    $@
	@$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	@echo   OBJCOPY $@
	@$(OBJCOPY) -O binary $< $@
	@$(POWERSHELL) -NoProfile -Command "$$l=(Get-Item '$@').Length; if ($$l -gt $(KERNEL_MAX)) { Write-Host 'kernel exceeds the 448 KiB stage-2 limit'; exit 1 }"

# â”€â”€ Disk image â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

$(IMAGE): $(BUILD_DIR)/boot.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/kernel.bin tools/build_image.ps1
	@echo   IMAGE $@
	@$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File "$(CURDIR)/tools/build_image.ps1" -Root "$(CURDIR)"

# â”€â”€ Run â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

run: all
	@echo   QEMU  windowed, scaled to fit
	@$(QEMU) $(QEMU_BASE) -display gtk,zoom-to-fit=on

run-native: all
	@echo   QEMU  windowed, native pixels
	@$(QEMU) $(QEMU_BASE)

run-scaled: run

run-fullscreen: all
	@echo   QEMU  fullscreen
	@$(QEMU) $(QEMU_BASE) -display gtk -full-screen

debug: all
	@echo   QEMU  paused, GDB stub on tcp::1234
	@$(QEMU) $(QEMU_BASE) -s -S

# â”€â”€ Utilities â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

font:
	@$(POWERSHELL) -NoProfile -ExecutionPolicy Bypass -File tools/genfont.ps1

size: $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/boot.bin
	@$(POWERSHELL) -NoProfile -Command "Get-Item '$(BUILD_DIR)/boot.bin','$(BUILD_DIR)/stage2.bin','$(BUILD_DIR)/kernel.bin' | Select-Object Name,Length | Format-Table -AutoSize"
	@$(POWERSHELL) -NoProfile -Command "$$l=(Get-Item '$(BUILD_DIR)/kernel.bin').Length; Write-Host ('kernel: {0} / $(KERNEL_MAX) bytes ({1:N1}% of budget)' -f $$l, ($$l*100/$(KERNEL_MAX)))"

clean:
	@$(POWERSHELL) -NoProfile -Command "if (Test-Path -LiteralPath '$(BUILD_DIR)') { Get-ChildItem -LiteralPath '$(BUILD_DIR)' | Where-Object { $$_.Name -ne 'font_preview.png' } | Remove-Item -Recurse -Force }"

help:
	@echo Altitude OS build targets:
	@echo   all             build the floppy image
	@echo   run             launch QEMU in a resizable window
	@echo   run-native      launch QEMU at native pixel size
	@echo   run-fullscreen  launch QEMU fullscreen
	@echo   debug           launch QEMU paused with a GDB stub
	@echo   size            show kernel/image size budget
	@echo   clean           remove build output
	@echo   font            regenerate include/font8x16.h
