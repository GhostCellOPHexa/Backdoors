/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "arch/cpu.h"
#include "drivers/vga_text.h"
#include "kernel/log.h"
#include "kernel/panic.h"

/* Evita recursão se a própria impressão de pânico falhar. */
static volatile int in_panic = 0;

__attribute__((noreturn)) void kpanic(const char *msg)
{
    __asm__ volatile ("cli");

    if (in_panic) {
        cpu_halt_forever();
    }
    in_panic = 1;

    vga_set_color(VGA_WHITE_ON_RED);
    kprintf("KERNEL PANIC: %s\n", msg);
    kprintf("System halted.\n");

    cpu_halt_forever();
}
