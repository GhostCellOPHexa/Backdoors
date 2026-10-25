/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_KERNEL_IDT_H
#define BD_KERNEL_IDT_H

#include <stdint.h>

/*
 * Quadro salvo pelo stub de interrupção (ordem de endereços crescentes).
 * Segmentos empilhados por último, então ficam primeiro.
 */
struct regs {
    uint32_t gs, fs, es, ds;                    /* segmentos */
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;   /* PUSHAD */
    uint32_t int_no, err_code;                  /* número vetor e código de erro (0 se não houver) */
    uint32_t eip, cs, eflags;                   /* quadro empilhado pela CPU */
};

#define IDT_VECTOR_IRQ_BASE 32
#define IDT_IRQ_COUNT       16

typedef void (*irq_handler_t)(struct regs *r);

/* Carrega a IDT: exceções 0..31 e IRQs 0..15 (vetores 32..47). */
void idt_init(void);

/* Chamado pelos stubs em interrupts.asm. Não deve ser chamado diretamente. */
void isr_dispatch(struct regs *r);

/* Registra o tratador de um IRQ (0..15). Passe NULL para remover. */
void irq_register(uint8_t irq, irq_handler_t handler);

#endif /* BD_KERNEL_IDT_H */
