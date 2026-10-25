/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_LANG_INT_INTERNAL_H
#define BD_LANG_INT_INTERNAL_H

/* Auxiliares internos do compilador, verificador e VM de .int (não é API pública). */

#include <stddef.h>
#include <stdint.h>

#include "lang/int.h"

extern const uint8_t int_operand_len[OP_COUNT];

uint16_t int_rd16(const uint8_t *p);
uint32_t int_rd32(const uint8_t *p);
uint32_t int_fnv1a(const uint8_t *p, size_t n);
void int_set_error(struct int_error *err, int line, const char *msg);
void int_set_error_hex(struct int_error *err, const char *msg, uint32_t value);

#endif /* BD_LANG_INT_INTERNAL_H */
