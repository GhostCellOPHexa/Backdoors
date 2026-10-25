/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdint.h>

#include "arch/cpu.h"
#include "drivers/pic.h"
#include "kernel/idt.h"
#include "kernel/log.h"
#include "kernel/panic.h"
#include "kernel/string.h"

#define IDT_ENTRIES     256
#define GATE_INTERRUPT  0x8E    /* P=1, DPL=0, tipo 32-bit interrupt gate */
#define KERNEL_CS       0x08
#define BREAKPOINT_VEC  3
#define PAGE_FAULT_VEC  14

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern void idt_flush(const struct idt_ptr *ptr);
extern const uint32_t interrupt_stub_table[32 + IDT_IRQ_COUNT];

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtr;
static irq_handler_t irq_handlers[IDT_IRQ_COUNT];

static const char *const exception_names[32] = {
    "Divide Error (#DE)",
    "Debug (#DB)",
    "Non-Maskable Interrupt",
    "Breakpoint (#BP)",
    "Overflow (#OF)",
    "BOUND Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",
    "Device Not Available (#NM)",
    "Double Fault (#DF)",
    "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",
    "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",
    "General Protection (#GP)",
    "Page Fault (#PF)",
    "Reserved",
    "x87 Floating-Point Error (#MF)",
    "Alignment Check (#AC)",
    "Machine Check (#MC)",
    "SIMD Floating-Point Exception (#XM)",
    "Virtualization Exception (#VE)",
    "Control Protection Exception (#CP)",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
};

static void set_gate(uint8_t vector, uint32_t handler)
{
    idt[vector].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector    = KERNEL_CS;
    idt[vector].zero        = 0;
    idt[vector].type_attr   = GATE_INTERRUPT;
    idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void irq_register(uint8_t irq, irq_handler_t handler)
{
    if (irq < IDT_IRQ_COUNT) {
        irq_handlers[irq] = handler;
    }
}

static void exception_fatal(const struct regs *r)
{
    const char *name = exception_names[r->int_no];
    kprintf("EXCEPTION %u: %s\n", r->int_no, name);
    kprintf("  error=0x%x eip=0x%x cs=0x%x eflags=0x%x\n",
            r->err_code, r->eip, r->cs, r->eflags);
    if (r->int_no == PAGE_FAULT_VEC) {
        kprintf("  cr2=0x%x\n", cpu_read_cr2());
    }
    kpanic(name);
}

void isr_dispatch(struct regs *r)
{
    if (r->int_no < 32) {
        if (r->int_no == BREAKPOINT_VEC) {
            klog("[exc] breakpoint at eip=0x%x (returning)\n", r->eip);
            return;
        }
        exception_fatal(r);
    }

    if (r->int_no < IDT_VECTOR_IRQ_BASE + IDT_IRQ_COUNT) {
        uint8_t irq = (uint8_t)(r->int_no - IDT_VECTOR_IRQ_BASE);
        if (irq_handlers[irq] != NULL) {
            irq_handlers[irq](r);
        }
        pic_send_eoi(irq);
        return;
    }

    kprintf("unhandled interrupt vector %u\n", r->int_no);
    kpanic("unhandled interrupt vector");
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));
    for (uint32_t v = 0; v < 32 + IDT_IRQ_COUNT; v++) {
        set_gate((uint8_t)v, interrupt_stub_table[v]);
    }

    idtr.limit = (uint16_t)(sizeof(idt) - 1);
    idtr.base  = (uint32_t)(uintptr_t)idt;
    idt_flush(&idtr);
}
