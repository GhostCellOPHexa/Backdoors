; SPDX-License-Identifier: GPL-3.0-or-later
;
; Stubs de interrupção: exceções (vetores 0..31) e IRQs (vetores 32..47).
; Cada stub deixa uma pilha uniforme: [int_no][err_code] + quadro da CPU.
; Depois salva registradores e chama isr_dispatch(struct regs *).

BITS 32

global idt_flush
global interrupt_stub_table
extern isr_dispatch

; Exceção sem código de erro: a CPU não empilha nada; empilhamos 0.
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

; Exceção com código de erro: a CPU já empilhou o código.
%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

; IRQ: vetor já remapeado pelo PIC (32 + n). Sem código de erro.
%macro IRQ 2
global irq%1
irq%1:
    push dword 0
    push dword %2
    jmp isr_common
%endmacro

section .text

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

isr_common:
    pushad                       ; EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI

    mov eax, ds
    push eax
    mov eax, es
    push eax
    mov eax, fs
    push eax
    mov eax, gs
    push eax

    mov ax, 0x10                 ; segmentos de dados do kernel
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                     ; struct regs *
    call isr_dispatch
    add esp, 4

    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax

    popad
    add esp, 8                   ; descarta int_no e err_code
    iret

; idt_flush(const struct idt_ptr *ptr)
idt_flush:
    mov eax, [esp + 4]
    lidt [eax]
    ret

section .data
align 4
; Tabela de endereços dos stubs, indexada pelo vetor (0..47).
interrupt_stub_table:
%assign i 0
%rep 32
    dd isr%[i]
%assign i i+1
%endrep
%assign i 0
%rep 16
    dd irq%[i]
%assign i i+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
