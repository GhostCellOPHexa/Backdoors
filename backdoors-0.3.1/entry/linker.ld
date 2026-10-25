/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Backdoors OS — script de linker do kernel (i386, Multiboot).
 * O kernel é carregado em 1 MiB físico, como o GRUB espera para kernels ELF.
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .text ALIGN(4096) : {
        KEEP(*(.multiboot))          /* cabeçalho Multiboot: primeiros 8 KiB do arquivo */
        *(.text .text.*)
    }

    .rodata ALIGN(4096) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4096) : {
        *(.data .data.*)
    }

    .bss ALIGN(4096) : {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
