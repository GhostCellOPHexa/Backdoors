/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_ARCH_CPU_H
#define BD_ARCH_CPU_H

#include <stdint.h>

/* Desativa interrupções e para a CPU para sempre. Não retorna. */
__attribute__((noreturn)) void cpu_halt_forever(void);

/* Habilita interrupções (STI). */
static inline void cpu_enable_interrupts(void)
{
    __asm__ volatile ("sti");
}

/* CPUID (cpuid.asm). cpu_cpuid só pode ser chamada se cpu_has_cpuid() for true. */
uint32_t cpu_has_cpuid(void);
void cpu_cpuid(uint32_t leaf, uint32_t out[4]);

/* Desabilita interrupções (CLI). */
static inline void cpu_disable_interrupts(void)
{
    __asm__ volatile ("cli");
}

/* Espera a próxima interrupção (STI; HLT atômico em relação ao IRQ). */
static inline void cpu_wait_for_interrupt(void)
{
    __asm__ volatile ("sti; hlt");
}

/* Seletor de código atual (CS). */
static inline uint16_t cpu_read_cs(void)
{
    uint16_t value;
    __asm__ volatile ("mov %%cs, %0" : "=r"(value));
    return value;
}

/* Endereço que causou a última falha de página. */
static inline uint32_t cpu_read_cr2(void)
{
    uint32_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

#endif /* BD_ARCH_CPU_H */
