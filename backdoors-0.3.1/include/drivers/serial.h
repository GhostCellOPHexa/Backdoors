/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_DRIVERS_SERIAL_H
#define BD_DRIVERS_SERIAL_H

#include <stdbool.h>

/*
 * Porta serial COM1 (0x3F8), 115200 bps, 8N1.
 * serial_init faz um teste em modo loopback e retorna false se a porta não responder.
 */
bool serial_init(void);

/* true se serial_init detectou a porta. */
bool serial_available(void);

/* Envia um caractere. '\n' é convertido em "\r\n". Sem efeito se a porta não foi inicializada. */
void serial_write_char(char c);

#endif /* BD_DRIVERS_SERIAL_H */
