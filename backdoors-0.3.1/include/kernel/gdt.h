/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_KERNEL_GDT_H
#define BD_KERNEL_GDT_H

/* Seletores da GDT (índice << 3 | RPL). */
#define GDT_SEL_KERNEL_CODE 0x08
#define GDT_SEL_KERNEL_DATA 0x10
#define GDT_SEL_USER_CODE   0x1B   /* índice 3, RPL 3 */
#define GDT_SEL_USER_DATA   0x23   /* índice 4, RPL 3 */

/* Instala a GDT plana (base 0, limite 4 GiB) e recarrega CS e os seletores de dados. */
void gdt_init(void);

#endif /* BD_KERNEL_GDT_H */
