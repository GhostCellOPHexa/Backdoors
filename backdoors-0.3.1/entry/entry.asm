; SPDX-License-Identifier: GPL-3.0-or-later
;
; Backdoors OS — ponto de entrada do kernel.
;
; O GRUB (protocolo Multiboot) entra aqui em modo protegido de 32 bits,
; com paginação desligada e interrupções desativadas:
;   EAX = 0x2BADB002 (magic do bootloader)
;   EBX = endereço físico da estrutura de informações Multiboot
;
; Ordem: zerar .bss -> configurar a pilha -> chamar kernel_main(magic, info).

BITS 32

global _start
extern kernel_main
extern __bss_start
extern __bss_end

section .text
_start:
    cli
    cld

    ; Preserva o magic em ESI. rep stosb altera EAX, ECX e EDI, mas não EBX/ESI.
    mov esi, eax

    ; O GRUB não garante .bss zerado: zeramos antes de usar qualquer variável global.
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    ; Pilha de 16 KiB em .bss. Com -8 e dois push (8 bytes), ESP fica alinhado
    ; em 16 bytes no momento do CALL, como exige a ABI System V i386.
    mov esp, stack_top
    sub esp, 8
    push ebx                      ; argumento 2: info Multiboot
    push esi                      ; argumento 1: magic Multiboot
    call kernel_main

    ; kernel_main não deve retornar. Se retornar, paramos a CPU.
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

; Marca a pilha como não executável (evita aviso do ld sobre pilha executável).
section .note.GNU-stack noalloc noexec nowrite progbits
