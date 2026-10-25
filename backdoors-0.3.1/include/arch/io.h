/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_ARCH_IO_H
#define BD_ARCH_IO_H

#include <stdint.h>

/* Acesso às portas de I/O x86. */

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

#endif /* BD_ARCH_IO_H */
