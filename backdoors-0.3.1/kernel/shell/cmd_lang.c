/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/keyboard.h"
#include "fs/vfs.h"
#include "kernel/log.h"
#include "lang/int.h"
#include "lang/int_internal.h"
#include "mm/heap.h"
#include "shell/builtins.h"

/*
 * Programas: python (compila e executa o subconjunto de Python), pyc (compila
 * para um arquivo .int), int (executa um .int) e intinfo (verifica e mostra o
 * cabeçalho). ESC interrompe a execução de qualquer um deles.
 */

#define IMAGE_MAX (INT_HEADER_SIZE + INT_MAX_CODE + INT_MAX_CONST)

static void usage(const char *text)
{
    kprintf("uso: %s\n", text);
}

static size_t text_len(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static bool text_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* Lê um arquivo inteiro para o heap. Retorna NULL e informa o erro se falhar. */
static uint8_t *load_file(const char *cmd, const char *path, size_t *len)
{
    struct vfs_stat st;
    int s = vfs_stat(path, &st);
    if (s != VFS_OK) {
        kprintf("%s: '%s': %s\n", cmd, path, vfs_strerror(s));
        return NULL;
    }
    if (st.is_dir) {
        kprintf("%s: '%s' e um diretorio\n", cmd, path);
        return NULL;
    }
    uint8_t *buf = kmalloc(st.size ? st.size : 1);
    if (buf == NULL) {
        kprintf("%s: memoria esgotada\n", cmd);
        return NULL;
    }
    size_t got = 0;
    s = vfs_read(path, 0, buf, st.size, &got);
    if (s != VFS_OK) {
        kfree(buf);
        kprintf("%s: '%s': %s\n", cmd, path, vfs_strerror(s));
        return NULL;
    }
    *len = got;
    return buf;
}

static void sink(const char *s, size_t n)
{
    kwrite(s, n);
}

/* Consultado pela VM: devolve 1 se ESC foi pressionado. */
static int stop_on_esc(void)
{
    int k;
    while ((k = keyboard_poll()) != -1) {
        if (k == KEY_ESC) {
            return 1;
        }
    }
    return 0;
}

static void show_error(const char *cmd, const struct int_error *err)
{
    if (err->line > 0) {
        kprintf("%s: linha %d: %s\n", cmd, err->line, err->msg);
    } else {
        kprintf("%s: %s\n", cmd, err->msg);
    }
}

/* Compila 'src' e executa. Retorna 0 em sucesso. */
static int compile_and_run(const char *cmd, const char *src, size_t len)
{
    uint8_t *img = kmalloc(IMAGE_MAX);
    if (img == NULL) {
        kprintf("%s: memoria esgotada\n", cmd);
        return -1;
    }
    struct int_error err;
    size_t img_len = 0;
    int rc = int_compile_python(src, len, img, IMAGE_MAX, &img_len, &err);
    if (rc == 0) {
        rc = int_run(img, img_len, sink, stop_on_esc, &err);
        if (rc != 0) {
            show_error(cmd, &err);
        }
    } else {
        show_error(cmd, &err);
    }
    kfree(img);
    return rc;
}

void cmd_python(int argc, char **argv)
{
    if (argc == 3 && text_eq(argv[1], "-c")) {
        compile_and_run("python", argv[2], text_len(argv[2]));
        return;
    }
    if (argc != 2) {
        usage("python ARQUIVO.py   ou   python -c \"codigo\"");
        return;
    }
    size_t len = 0;
    uint8_t *src = load_file("python", argv[1], &len);
    if (src == NULL) {
        return;
    }
    compile_and_run("python", (const char *)src, len);
    kfree(src);
}

/* Nome do arquivo .int a partir do fonte: troca ".py" por ".int" (ou acrescenta). */
static bool int_name(const char *src, char *dst, size_t cap)
{
    size_t n = text_len(src);
    size_t keep = n;
    if (n >= 3 && src[n - 3] == '.' && src[n - 2] == 'p' && src[n - 1] == 'y') {
        keep = n - 3;
    }
    if (keep + 5 > cap) {
        return false;
    }
    for (size_t i = 0; i < keep; i++) {
        dst[i] = src[i];
    }
    dst[keep] = '.';
    dst[keep + 1] = 'i';
    dst[keep + 2] = 'n';
    dst[keep + 3] = 't';
    dst[keep + 4] = '\0';
    return true;
}

void cmd_pyc(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        usage("pyc ARQUIVO.py [SAIDA.int]");
        return;
    }
    char out_name[VFS_PATH_MAX + 1];
    if (argc == 3) {
        size_t n = text_len(argv[2]);
        if (n > VFS_PATH_MAX) {
            kprintf("pyc: nome longo demais\n");
            return;
        }
        for (size_t i = 0; i <= n; i++) {
            out_name[i] = argv[2][i];
        }
    } else if (!int_name(argv[1], out_name, sizeof(out_name))) {
        kprintf("pyc: nome longo demais\n");
        return;
    }

    size_t len = 0;
    uint8_t *src = load_file("pyc", argv[1], &len);
    if (src == NULL) {
        return;
    }
    uint8_t *img = kmalloc(IMAGE_MAX);
    if (img == NULL) {
        kfree(src);
        kprintf("pyc: memoria esgotada\n");
        return;
    }
    struct int_error err;
    size_t img_len = 0;
    if (int_compile_python((const char *)src, len, img, IMAGE_MAX, &img_len, &err) != 0) {
        show_error("pyc", &err);
    } else {
        int st = vfs_write_file(out_name, img, img_len);
        if (st != VFS_OK) {
            kprintf("pyc: '%s': %s\n", out_name, vfs_strerror(st));
        } else {
            kprintf("pyc: gravado %s (%u bytes)\n", out_name, (unsigned)img_len);
        }
    }
    kfree(img);
    kfree(src);
}

void cmd_int(int argc, char **argv)
{
    if (argc != 2) {
        usage("int ARQUIVO.int");
        return;
    }
    size_t len = 0;
    uint8_t *img = load_file("int", argv[1], &len);
    if (img == NULL) {
        return;
    }
    struct int_error err;
    if (int_run(img, len, sink, stop_on_esc, &err) != 0) {
        show_error("int", &err);
    }
    kfree(img);
}

void cmd_intinfo(int argc, char **argv)
{
    if (argc != 2) {
        usage("intinfo ARQUIVO.int");
        return;
    }
    size_t len = 0;
    uint8_t *img = load_file("intinfo", argv[1], &len);
    if (img == NULL) {
        return;
    }
    struct int_info info;
    struct int_error err;
    if (int_verify(img, len, &info, &err) != 0) {
        show_error("intinfo", &err);
    } else {
        kprintf("formato .int versao %u, cabecalho %u bytes\n", INT_VERSION, INT_HEADER_SIZE);
        kprintf("codigo: %u bytes, constantes: %u bytes\n",
                (unsigned)info.code_size, (unsigned)info.const_size);
        kprintf("variaveis: %u, instrucoes: %u\n", (unsigned)info.slots, (unsigned)info.instructions);
        kprintf("checksum: 0x%08X (confere)\n", (unsigned)info.checksum);
    }
    kfree(img);
}
