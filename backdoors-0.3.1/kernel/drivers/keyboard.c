/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stdint.h>

#include "arch/cpu.h"
#include "arch/io.h"
#include "drivers/keyboard.h"
#include "drivers/pic.h"
#include "kernel/idt.h"

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64
#define KBD_OUT_FULL    0x01
#define KBD_IRQ         1

#define QUEUE_SIZE 64

/* Mapas de scancode set 1 (tecla pressionada, sem Shift). */
static const char map_plain[128] = {
    [0x01] = KEY_ESC,
    [0x02] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
    [0x1C] = '\n',
    [0x1E] = 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    [0x2B] = '\\',
    [0x2C] = 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    [0x39] = ' ',
};

/* Mesmas teclas com Shift. */
static const char map_shift[128] = {
    [0x01] = KEY_ESC,
    [0x02] = '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
    [0x1C] = '\n',
    [0x1E] = 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    [0x2B] = '|',
    [0x2C] = 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    [0x39] = ' ',
};

#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_CAPSLOCK 0x3A
#define SC_EXTENDED 0xE0

static volatile int queue[QUEUE_SIZE];
static volatile unsigned head;   /* próxima posição de escrita (IRQ) */
static volatile unsigned tail;   /* próxima posição de leitura (shell) */

static volatile bool extended;
static volatile bool shift_held;
static volatile bool caps_on;
static bool ready;

static void queue_push(int key)
{
    unsigned next = (head + 1) % QUEUE_SIZE;
    if (next == tail) {
        return;                  /* fila cheia: descarta a tecla */
    }
    queue[head] = key;
    head = next;
}

static void handle_extended(uint8_t code, bool release)
{
    if (release) {
        return;
    }
    switch (code) {
    case 0x48: queue_push(KEY_UP);    break;
    case 0x50: queue_push(KEY_DOWN);  break;
    case 0x4B: queue_push(KEY_LEFT);  break;
    case 0x4D: queue_push(KEY_RIGHT); break;
    case 0x47: queue_push(KEY_HOME);  break;
    case 0x4F: queue_push(KEY_END);   break;
    case 0x49: queue_push(KEY_PGUP);  break;
    case 0x51: queue_push(KEY_PGDN);  break;
    case 0x53: queue_push(KEY_DEL);   break;
    default:   break;
    }
}

static void keyboard_irq(struct regs *r)
{
    (void)r;
    uint8_t sc = inb(KBD_DATA_PORT);

    if (sc == SC_EXTENDED) {
        extended = true;
        return;
    }

    bool release = (sc & 0x80) != 0;
    uint8_t code = sc & 0x7F;

    if (extended) {
        extended = false;
        handle_extended(code, release);
        return;
    }

    if (code == SC_LSHIFT || code == SC_RSHIFT) {
        shift_held = !release;
        return;
    }
    if (code == SC_CAPSLOCK) {
        if (!release) {
            caps_on = !caps_on;
        }
        return;
    }
    if (release) {
        return;
    }

    char c = shift_held ? map_shift[code] : map_plain[code];
    if (c == 0) {
        return;
    }
    /* Letras: maiúscula se (CapsLock XOR Shift), minúscula caso contrário.
     * Os mapas trazem a letra no caso do Shift; aqui normalizamos o caso final. */
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        bool upper = caps_on != shift_held;
        if (upper && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        } else if (!upper && c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
    }
    queue_push((unsigned char)c);
}

void keyboard_init(void)
{
    /* Descarta bytes pendentes no controlador antes de habilitar a IRQ. */
    while (inb(KBD_STATUS_PORT) & KBD_OUT_FULL) {
        (void)inb(KBD_DATA_PORT);
    }
    irq_register(KBD_IRQ, keyboard_irq);
    pic_unmask(KBD_IRQ);
    ready = true;
}

bool keyboard_available(void)
{
    return ready;
}

int keyboard_poll(void)
{
    int key = -1;
    cpu_disable_interrupts();
    if (head != tail) {
        key = queue[tail];
        tail = (tail + 1) % QUEUE_SIZE;
    }
    cpu_enable_interrupts();
    return key;
}

int keyboard_getkey(void)
{
    for (;;) {
        cpu_disable_interrupts();
        if (head != tail) {
            int key = queue[tail];
            tail = (tail + 1) % QUEUE_SIZE;
            cpu_enable_interrupts();
            return key;
        }
        cpu_enable_interrupts();
        cpu_wait_for_interrupt();
    }
}
