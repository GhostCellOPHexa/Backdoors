# SPDX-License-Identifier: GPL-3.0-or-later
#
# Backdoors OS — sistema de build
#
# Requisitos: gcc com suporte a -m32, nasm, ld (binutils), grub-mkrescue
# (com xorriso), grub-file e qemu-system-i386. Veja BUILDING.md.

NAME          := backdoors
BUILD         := build
KERNEL        := $(BUILD)/kernel.elf
ISO           := $(BUILD)/$(NAME).iso
ISO_ROOT      := $(BUILD)/isoroot
INITRAMFS     := $(BUILD)/initramfs.cpio
INITRAMFS_SRC := $(shell find initramfs -type f 2>/dev/null | sort)
PKG_SRC       := $(shell find packages -type f 2>/dev/null | sort)
PKG_HELLO     := $(BUILD)/packages/hello-1.0.0.bkg
STAGING       := $(BUILD)/staging
STAMP         := $(BUILD)/staging.stamp

CC            := gcc
AS            := nasm
LD            := ld
GRUB_MKRESCUE ?= grub-mkrescue
GRUB_FILE     ?= grub-file
QEMU          ?= qemu-system-i386
PYTHON        ?= python3

# Kernel freestanding para i686. Não usa libc nem bibliotecas do hospedeiro.
# -fno-tree-loop-distribute-patterns evita que o GCC transforme laços em
# chamadas a memset/memcpy dentro das próprias implementações dessas funções.
CFLAGS := -m32 -std=gnu11 -ffreestanding -O2 \
          -Wall -Wextra -Werror \
          -fno-stack-protector -fno-pic -fno-pie -fno-common \
          -fno-asynchronous-unwind-tables \
          -fno-tree-loop-distribute-patterns \
          -Iinclude -MMD -MP $(EXTRA_CFLAGS)

ASFLAGS := -f elf32 -w+all
LDFLAGS := -m elf_i386 -nostdlib -z max-page-size=4096 -T entry/linker.ld

C_SRCS   := $(shell find kernel -name '*.c' | sort)
ASM_SRCS := $(shell find boot entry kernel -name '*.asm' | sort)
OBJS     := $(patsubst %.c,$(BUILD)/obj/%.o,$(C_SRCS)) \
            $(patsubst %.asm,$(BUILD)/obj/%.o,$(ASM_SRCS))
DEPS     := $(OBJS:.o=.d)

.PHONY: all iso run debug test clean check-tools initramfs

all: $(KERNEL)

check-tools:
	@command -v $(CC) >/dev/null 2>&1 || { echo "ERRO: '$(CC)' não encontrado. Veja BUILDING.md."; exit 1; }
	@echo 'int f(void){return 0;}' | $(CC) -m32 -ffreestanding -c -x c - -o /dev/null 2>/dev/null \
		|| { echo "ERRO: '$(CC) -m32 -ffreestanding' não funciona. Veja BUILDING.md."; exit 1; }
	@for t in $(AS) $(LD) $(GRUB_MKRESCUE) $(GRUB_FILE) $(QEMU); do \
		command -v $$t >/dev/null 2>&1 || { echo "ERRO: ferramenta '$$t' não encontrada. Veja BUILDING.md."; exit 1; }; \
	done
	@echo "Ferramentas OK."

$(KERNEL): $(OBJS) entry/linker.ld
	@mkdir -p $(dir $@)
	@echo "LD    $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.asm
	@mkdir -p $(dir $@)
	@echo "AS    $<"
	@$(AS) $(ASFLAGS) $< -o $@

iso: $(ISO)

initramfs: $(INITRAMFS)

# Pacote de exemplo (.bkg), copiado para /repo no initramfs.
$(PKG_HELLO): $(PKG_SRC) tools/mkpkg.py tools/mkinitramfs.py
	@mkdir -p $(dir $@)
	@echo "PKG   $@"
	@$(PYTHON) tools/mkpkg.py packages/hello hello 1.0.0 $@ "Pacote de exemplo" >/dev/null

$(STAMP): $(INITRAMFS_SRC) $(PKG_HELLO)
	rm -rf $(STAGING)
	mkdir -p $(STAGING)/repo
	cp -r initramfs/. $(STAGING)/
	cp $(PKG_HELLO) $(STAGING)/repo/
	touch $@

$(INITRAMFS): $(STAMP) tools/mkinitramfs.py
	@echo "CPIO  $@"
	@$(PYTHON) tools/mkinitramfs.py $(STAGING) $@ >/dev/null

$(ISO): $(KERNEL) $(INITRAMFS) iso/boot/grub/grub.cfg
	$(GRUB_FILE) --is-x86-multiboot $(KERNEL)
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $(KERNEL) $(ISO_ROOT)/boot/kernel.elf
	cp $(INITRAMFS) $(ISO_ROOT)/boot/initramfs.cpio
	cp iso/boot/grub/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	@echo "ISO   $@"
	@$(GRUB_MKRESCUE) -o $@ $(ISO_ROOT) > $(BUILD)/grub-mkrescue.log 2>&1 \
		|| { cat $(BUILD)/grub-mkrescue.log; echo "ERRO: grub-mkrescue falhou (log acima)."; exit 1; }

run: iso
	$(QEMU) -cdrom $(ISO) -serial stdio -no-reboot -m 32

debug: iso
	@echo "QEMU aguardando GDB em localhost:1234 (ex.: gdb $(KERNEL), depois 'target remote :1234')"
	$(QEMU) -cdrom $(ISO) -serial stdio -no-reboot -m 32 -s -S

test: iso
	sh tests/unit/run_unit.sh
	sh tests/boot/test_boot.sh
	$(PYTHON) tests/boot/shell_test.py $(ISO) $(BUILD)/test/shell
	$(PYTHON) tests/boot/apps_test.py $(ISO) $(BUILD)/test/apps

# Build separada (diretório próprio) com o caminho de pânico ativado.
test-panic:
	$(MAKE) BUILD=build/panic EXTRA_CFLAGS=-DBD_TEST_PANIC iso

clean:
	rm -rf $(BUILD)

-include $(DEPS)
