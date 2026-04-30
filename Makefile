
UNAME_M := $(shell uname -m)

# Allow override: `make PREFIX=i386-unknown-elf-`
# (Only auto-detect on aarch64; native x86 builds use system gcc.)
ifeq ($(UNAME_M),aarch64)

# Known cross-compiler prefixes on popular distros/toolchains:
#  - Ubuntu/Debian:       i686-linux-gnu-
#  - Gentoo (crossdev):   i386-unknown-elf-
#  - Generic ELF:         i686-elf- / i386-elf-
#  - Some Linux triplets: i386-pc-linux-gnu- / i686-pc-linux-gnu-
CROSS_CANDIDATES := \
  i686-linux-gnu- \
  i386-unknown-elf- \
  i686-elf- \
  i386-elf- \
  i386-pc-linux-gnu- \
  i686-pc-linux-gnu-

# Only auto-set PREFIX if the user hasn't provided one.
ifeq ($(origin PREFIX), undefined)
PREFIX := $(firstword \
  $(foreach p,$(CROSS_CANDIDATES), \
    $(if $(shell command -v $(p)gcc >/dev/null 2>&1 && echo yes),$(p))))
endif

# Friendly failure if nothing was found.
ifndef PREFIX
$(error No i386 cross-compiler found on aarch64. \
Install one (e.g., i686-linux-gnu-gcc or i386-unknown-elf-gcc) \
or run: make PREFIX=<triplet->)
endif

BOOTIMG := /usr/local/grub/lib/grub/i386-pc/boot.img
GRUBLOC := /usr/local/grub/bin/

else
PREFIX  ?=
BOOTIMG := /usr/lib/grub/i386-pc/boot.img
GRUBLOC :=
endif


CC := $(PREFIX)gcc
LD := $(PREFIX)ld
OBJDUMP := $(PREFIX)objdump
OBJCOPY := $(PREFIX)objcopy
SIZE := $(PREFIX)size
CONFIGS := -DCONFIG_HEAP_SIZE=4096
CFLAGS := -ffreestanding -mgeneral-regs-only -mno-mmx -m32 -march=i386 -fno-pie -fno-stack-protector -g3 -Wall 

ODIR = obj
SDIR = src

OBJS = \
	kernel_main.o \
	terminal.o \
	interrupt.o \
	page.o \
	fat.o \
	sd.o \
	ide.o \
	string.o \

# Make sure to keep a blank line here after OBJS list

OBJ = $(patsubst %,$(ODIR)/%,$(OBJS))

$(ODIR)/%.o: $(SDIR)/%.c
	$(CC) $(CFLAGS) -c -g -o $@ $^

$(ODIR)/%.o: $(SDIR)/%.s
	nasm -f elf32 -g -o $@ $^ 


all: bin rootfs.img

bin: obj $(OBJ)
	$(LD) -melf_i386  obj/* -Tkernel.ld -o kernel
	$(SIZE) kernel

obj:
	mkdir -p obj

rootfs.img:
	dd if=/dev/zero of=rootfs.img bs=1M count=32
	$(GRUBLOC)grub-mkimage -p "(hd0,msdos1)/boot" -o grub.img -O i386-pc normal biosdisk multiboot multiboot2 configfile fat exfat part_msdos
	dd if=$(BOOTIMG) of=rootfs.img conv=notrunc
	dd if=grub.img of=rootfs.img conv=notrunc bs=512 seek=1 #########
	echo 'start=2048, type=83, bootable' | sfdisk rootfs.img
	mkfs.vfat --offset 2048 -F16 rootfs.img
	mcopy -i rootfs.img@@1M kernel ::/
	mcopy -i rootfs.img@@1M TEST.TXT ::/
	mmd -i rootfs.img@@1M boot 
	mcopy -i rootfs.img@@1M grub.cfg ::/boot
	@echo " -- BUILD COMPLETED SUCCESSFULLY --"


run:
	qemu-system-i386 -hda rootfs.img

debug:
	./launch_qemu.sh

clean:
	rm -f grub.img kernel rootfs.img obj/*
