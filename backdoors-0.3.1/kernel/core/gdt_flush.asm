; SPDX-License-Identifier: GPL-3.0-or-later
;
; gdt_flush(const struct gdt_ptr *ptr)
; Carrega a GDT com LGDT e recarrega CS (far jump) e os seletores de dados.

BITS 32

global gdt_flush

section .text
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]

    mov ax, 0x10                 ; seletor de dados do kernel
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.reload_cs          ; recarrega CS com o seletor de código do kernel
.reload_cs:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
