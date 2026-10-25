/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "arch/cpu.h"

__attribute__((noreturn)) void cpu_halt_forever(void)
{
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
