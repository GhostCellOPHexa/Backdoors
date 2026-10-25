/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stdint.h>

#include "arch/io.h"
#include "drivers/serial.h"

/* Registradores do UART 16550 relativos à base COM1. */
#define COM1_BASE   0x3F8
#define REG_DATA    0   /* RBR/THR; com DLAB=1, divisor baixo */
#define REG_IER     1   /* interrupt enable; com DLAB=1, divisor alto */
#define REG_FCR     2
#define REG_LCR     3
#define REG_MCR     4
#define REG_LSR     5

#define LCR_DLAB    0x80
#define LCR_8N1     0x03
#define LSR_DATA_READY 0x01
#define LSR_THR_EMPTY  0x20
#define MCR_LOOPBACK   0x10
#define MCR_NORMAL     0x0B   /* DTR | RTS | OUT2 */

/* Limite de espera por registrador ocupado. Evita travar o kernel sem UART. */
#define SPIN_LIMIT 100000u

static bool serial_ready = false;

static bool loopback_selftest(void)
{
    const uint8_t probe = 0xAE;
    outb(COM1_BASE + REG_MCR, MCR_LOOPBACK);
    outb(COM1_BASE + REG_DATA, probe);

    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        if (inb(COM1_BASE + REG_LSR) & LSR_DATA_READY) {
            uint8_t echoed = inb(COM1_BASE + REG_DATA);
            outb(COM1_BASE + REG_MCR, MCR_NORMAL);
            return echoed == probe;
        }
    }
    outb(COM1_BASE + REG_MCR, MCR_NORMAL);
    return false;
}

bool serial_init(void)
{
    outb(COM1_BASE + REG_IER, 0x00);      /* sem interrupções */
    outb(COM1_BASE + REG_LCR, LCR_DLAB);  /* habilita acesso ao divisor */
    outb(COM1_BASE + REG_DATA, 0x01);     /* divisor 1 -> 115200 bps (byte baixo) */
    outb(COM1_BASE + REG_IER, 0x00);      /* divisor alto = 0 */
    outb(COM1_BASE + REG_LCR, LCR_8N1);   /* 8 bits, sem paridade, 1 stop bit */
    outb(COM1_BASE + REG_FCR, 0xC7);      /* FIFO habilitado e limpo */

    serial_ready = loopback_selftest();
    return serial_ready;
}

bool serial_available(void)
{
    return serial_ready;
}

void serial_write_char(char c)
{
    if (!serial_ready) {
        return;
    }
    if (c == '\n') {
        serial_write_char('\r');
    }
    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        if (inb(COM1_BASE + REG_LSR) & LSR_THR_EMPTY) {
            outb(COM1_BASE + REG_DATA, (uint8_t)c);
            return;
        }
    }
    /* Timeout: o caractere é descartado. Não ocorre em QEMU com COM1 presente. */
}
