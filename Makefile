# ─────────────────────────────────────────────────────────────
#  Makefile — bare-metal x86 kernel
# ─────────────────────────────────────────────────────────────
 
# ── Toolchain ─────────────────────────────────────────────────
CC      := gcc
AS      := nasm
LD      := ld
QEMU    := qemu-system-i386
 
# ── Targets & Directories ─────────────────────────────────────
TARGET  := kernel.bin
ISO     := kernel.iso
BUILD   := build
ISODIR  := $(BUILD)/iso/boot/grub
 
# ── Sources ───────────────────────────────────────────────────
C_SRCS  := kernel_main.c
ASM_SRCS := boot.s
 
C_OBJS  := $(patsubst %.c,  $(BUILD)/%.o, $(C_SRCS))
ASM_OBJS := $(patsubst %.s, $(BUILD)/%.o, $(ASM_SRCS))
OBJS    := $(ASM_OBJS) $(C_OBJS)
 
# ── Flags ─────────────────────────────────────────────────────
CFLAGS  := -m32 \
            -std=c99 \
            -ffreestanding \
            -fno-builtin \
            -fno-stack-protector \
            -fno-pic \
            -Wall \
            -Wextra \
            -O2
 
ASFLAGS := -f elf32
 
LDFLAGS := -m elf_i386 \
            -T linker.ld \
            --oformat binary \
            -nostdlib
 
# ── GRUB config (written on the fly) ──────────────────────────
define GRUB_CFG
set timeout=0
set default=0
 
menuentry "My Kernel" {
    multiboot /boot/$(TARGET)
    boot
}
endef
export GRUB_CFG
 
# ══════════════════════════════════════════════════════════════
#  Default target
# ══════════════════════════════════════════════════════════════
.PHONY: all
all: $(BUILD)/$(TARGET)
 
# ── Link ──────────────────────────────────────────────────────
$(BUILD)/$(TARGET): $(OBJS) linker.ld | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "[LD]  $@"
 
# ── Compile C ─────────────────────────────────────────────────
$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "[CC]  $<"
 
# ── Assemble ──────────────────────────────────────────────────
$(BUILD)/%.o: %.s | $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@
	@echo "[AS]  $<"
 
# ── Create build dir ──────────────────────────────────────────
$(BUILD):
	mkdir -p $(BUILD)
 
# ══════════════════════════════════════════════════════════════
#  ISO  (requires grub-mkrescue + xorriso)
# ══════════════════════════════════════════════════════════════
.PHONY: iso
iso: $(BUILD)/$(TARGET)
	mkdir -p $(ISODIR)
	cp $(BUILD)/$(TARGET) $(BUILD)/iso/boot/$(TARGET)
	echo "$$GRUB_CFG" > $(ISODIR)/grub.cfg
	grub-mkrescue -o $(BUILD)/$(ISO) $(BUILD)/iso
	@echo "[ISO] $(BUILD)/$(ISO)"
 
# ══════════════════════════════════════════════════════════════
#  Run in QEMU
# ══════════════════════════════════════════════════════════════
.PHONY: run
run: $(BUILD)/$(TARGET)
	$(QEMU) -kernel $(BUILD)/$(TARGET)
 
.PHONY: run-iso
run-iso: iso
	$(QEMU) -cdrom $(BUILD)/$(ISO)
 
# Run with serial output redirected to stdio (useful for debugging)
.PHONY: debug
debug: $(BUILD)/$(TARGET)
	$(QEMU) -kernel $(BUILD)/$(TARGET) \
	        -serial stdio \
	        -d int,cpu_reset \
	        -no-reboot
 
# ══════════════════════════════════════════════════════════════
#  Inspect
# ══════════════════════════════════════════════════════════════
.PHONY: disasm
disasm: $(BUILD)/$(TARGET)
	objdump -m i386 -b binary -D $(BUILD)/$(TARGET) | less
 
.PHONY: nm
nm: $(BUILD)/$(TARGET)
	nm -n $(BUILD)/$(TARGET) 2>/dev/null || objdump -t $(BUILD)/$(TARGET)
 
# ══════════════════════════════════════════════════════════════
#  Clean
# ══════════════════════════════════════════════════════════════
.PHONY: clean
clean:
	rm -rf $(BUILD)
	@echo "[CLEAN] done"
 
.PHONY: distclean
distclean: clean
	rm -f $(BUILD)/$(ISO)
 
# ══════════════════════════════════════════════════════════════
#  Help
# ══════════════════════════════════════════════════════════════
.PHONY: help
help:
	@echo ""
	@echo "  make            — build kernel.bin"
	@echo "  make iso        — build bootable ISO (needs grub-mkrescue)"
	@echo "  make run        — run kernel in QEMU (-kernel)"
	@echo "  make run-iso    — run ISO in QEMU (-cdrom)"
	@echo "  make debug      — run with serial + interrupt logging"
	@echo "  make disasm     — disassemble kernel binary"
	@echo "  make clean      — remove build artifacts"
	@echo ""
 
