; SPDX-License-Identifier: GPL-3.0-or-later
;
; Backdoors OS — cabeçalho Multiboot 1.
; O GRUB procura este cabeçalho nos primeiros 8 KiB do kernel ELF.
; Flags do cabeçalho: somente bit0 (alinhar módulos em 4 KiB) e bit1 (pedir
; memória básica). O mapa de memória (bit6) é um flag de *informação* devolvido
; pelo bootloader, não um pedido no cabeçalho: o GRUB rejeita 0x43 com
; "unsupported flag". O QEMU com -kernel aceita; o GRUB não.

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000003   ; bit0: alinhar módulos; bit1: info de memória

section .multiboot progbits alloc noexec nowrite align=4
    dd MB_MAGIC
    dd MB_FLAGS
    dd -(MB_MAGIC + MB_FLAGS)     ; checksum: magic + flags + checksum == 0 (mod 2^32)

section .note.GNU-stack noalloc noexec nowrite progbits
