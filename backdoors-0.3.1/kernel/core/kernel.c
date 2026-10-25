/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stdint.h>

#include "arch/cpu.h"
#include "drivers/keyboard.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/vga_text.h"
#include "fs/vfs.h"
#include "kernel/gdt.h"
#include "kernel/idt.h"
#include "kernel/kernel.h"
#include "kernel/log.h"
#include "kernel/multiboot.h"
#include "kernel/panic.h"
#include "kernel/version.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "shell/shell.h"

/*
 * Mensagens do kernel em ASCII: a VGA usa a codepage 437, onde bytes UTF-8
 * (acentos) aparecem como caracteres errados.
 *
 * Os blocos BD_TEST_* existem só em builds de teste (ver tests/boot/test_boot.sh).
 */

#define TIMER_HZ    100
#define HEAP_BYTES   (4u * 1024u * 1024u)

/* Carrega todos os módulos Multiboot (o initramfs) no sistema de arquivos. -1 se malformado. */
static int load_initramfs(const struct multiboot_info *mbi)
{
    int total = 0;
    if (!(mbi->flags & MULTIBOOT_INFO_MODS)) {
        return 0;
    }
    const struct multiboot_module *mods = (const struct multiboot_module *)(uintptr_t)mbi->mods_addr;
    for (uint32_t i = 0; i < mbi->mods_count; i++) {
        const uint8_t *start = (const uint8_t *)(uintptr_t)mods[i].mod_start;
        size_t len = (size_t)(mods[i].mod_end - mods[i].mod_start);
        int n = vfs_load_cpio(start, len);
        if (n < 0) {
            return -1;
        }
        total += n;
    }
    return total;
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info)
{
    vga_init();

    bool serial_ok = serial_init();
    if (serial_ok) {
        klog("[boot] COM1 initialized (115200 8N1)\n");
    } else {
        kprintf("WARNING: COM1 serial port not available (loopback self-test failed)\n");
    }

#ifdef BD_TEST_PANIC
    kpanic("test panic requested at build time");
#endif

    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kpanic("kernel was not loaded by a Multiboot bootloader");
    }
    klog("[boot] Multiboot bootloader detected, info at 0x%x\n", multiboot_info);

    /* Fase 2: segmentação, interrupções e temporização. */
    gdt_init();
    klog("[boot] GDT loaded (CS=0x%x)\n", cpu_read_cs());

    idt_init();
    pic_init();
    timer_init(TIMER_HZ);
    klog("[boot] IDT loaded, PIC remapped, PIT at %u Hz\n", TIMER_HZ);

    cpu_enable_interrupts();

#ifdef BD_TEST_INT3
    __asm__ volatile ("int3");
    klog("[test] breakpoint returned\n");
#endif

#ifdef BD_TEST_DIV0
    __asm__ volatile (
        "xor %%edx, %%edx\n\t"
        "mov $1, %%eax\n\t"
        "xor %%ecx, %%ecx\n\t"
        "div %%ecx\n\t"
        : : : "eax", "ecx", "edx", "cc");
#endif

    timer_wait_ticks(10);
    klog("[boot] timer: %u ticks observed\n", timer_ticks());

    /* Fase 4: memória física e heap. */
    struct multiboot_info *mbi = (struct multiboot_info *)(uintptr_t)multiboot_info;
    pmm_init(mbi);
    klog("[boot] memory: %u frames usable, %u free\n",
         pmm_total_frames(), pmm_free_frames());
    if (!heap_init(HEAP_BYTES)) {
        kpanic("nao ha memoria contigua para o heap do kernel");
    }
    if (!heap_selftest()) {
        kpanic("heap self-test falhou");
    }
    klog("[boot] heap %u KiB ready, self-test passed\n", HEAP_BYTES / 1024);

    /* Fase 5: sistema de arquivos em memória, preenchido pelo initramfs (módulo do GRUB). */
    vfs_init();
    int entries = load_initramfs(mbi);
    if (entries < 0) {
        kpanic("initramfs malformado");
    }
    klog("[boot] initramfs: %d entries loaded\n", entries);

    /* Fase 3: teclado e shell. */
    keyboard_init();
    klog("[boot] keyboard ready (IRQ1)\n");

    kprintf("%s\n", BD_OS_NAME);
    kprintf("Version %s\n", BD_VERSION);
    kprintf("Kernel initialized.\n");
    kprintf("System ready.\n");

    klog("[boot] phase 3: shell started\n");
    shell_run();
}
