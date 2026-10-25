/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/serial.h"
#include "drivers/vga_text.h"
#include "kernel/log.h"

typedef void (*emit_fn)(char c);

static void emit_serial(char c)
{
    serial_write_char(c);
}

static void emit_console(char c)
{
    serial_write_char(c);
    vga_putc(c);
}

static void emit_str(emit_fn emit, const char *s)
{
    if (s == NULL) {
        s = "(null)";
    }
    while (*s) {
        emit(*s++);
    }
}

/* Emite 'n' caracteres de 'digits' (já na ordem correta), com preenchimento até 'width'. */
static void emit_padded(emit_fn emit, const char *digits, size_t n, unsigned width, bool zero)
{
    for (size_t pad = (n < width) ? width - n : 0; pad > 0; pad--) {
        emit(zero ? '0' : ' ');
    }
    for (size_t i = 0; i < n; i++) {
        emit(digits[i]);
    }
}

static void emit_uint(emit_fn emit, uint32_t value, uint32_t base, bool upper,
                      unsigned width, bool zero)
{
    static const char lower[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *table = upper ? upper_digits : lower;

    char buf[32];
    size_t n = 0;
    do {
        buf[n++] = table[value % base];
        value /= base;
    } while (value != 0);

    /* buf está em ordem inversa: inverte antes de emitir. */
    char out[32];
    for (size_t i = 0; i < n; i++) {
        out[i] = buf[n - 1 - i];
    }
    emit_padded(emit, out, n, width, zero);
}

static void emit_int(emit_fn emit, int value, unsigned width, bool zero)
{
    uint32_t magnitude;
    if (value < 0) {
        magnitude = 0u - (uint32_t)value;
        /* Sinal ocupa uma posição da largura; com zeros, o sinal vem antes. */
        emit('-');
        if (width > 0) {
            width--;
        }
    } else {
        magnitude = (uint32_t)value;
    }
    emit_uint(emit, magnitude, 10, false, width, zero);
}

static void vformat(emit_fn emit, const char *fmt, va_list ap)
{
    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            emit(*p);
            continue;
        }
        p++;

        /* Flags e largura: '0' opcional, depois dígitos da largura (ex.: %02u, %04u). */
        bool zero = false;
        unsigned width = 0;
        if (*p == '0') {
            zero = true;
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (unsigned)(*p - '0');
            p++;
        }

        switch (*p) {
        case 's':
            emit_str(emit, va_arg(ap, const char *));
            break;
        case 'c':
            emit((char)va_arg(ap, int));
            break;
        case 'd':
            emit_int(emit, va_arg(ap, int), width, zero);
            break;
        case 'u':
            emit_uint(emit, va_arg(ap, unsigned int), 10, false, width, zero);
            break;
        case 'x':
            emit_uint(emit, va_arg(ap, unsigned int), 16, false, width, zero);
            break;
        case 'X':
            emit_uint(emit, va_arg(ap, unsigned int), 16, true, width, zero);
            break;
        case '%':
            emit('%');
            break;
        case '\0':
            /* '%' no fim da string: encerra sem ler além do terminador. */
            return;
        default:
            /* Conversão desconhecida: imprime literalmente, sem consumir argumento. */
            emit('%');
            emit(*p);
            break;
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vformat(emit_console, fmt, ap);
    va_end(ap);
}

void klog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vformat(emit_serial, fmt, ap);
    va_end(ap);
}

void kwrite(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        emit_console(s[i]);
    }
}
