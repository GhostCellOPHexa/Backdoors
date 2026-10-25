/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/vga_text.h"
#include "fs/vfs.h"
#include "kernel/log.h"
#include "mm/heap.h"
#include "shell/commands.h"
#include "shell/parser.h"
#include "shell/script.h"

#define MAX_VARS     16
#define VAR_NAME_MAX 15
#define VAR_VALUE_MAX 63
#define LINE_MAX     256
#define MAX_ARGS     16
#define MAX_DEPTH    4

struct variable {
    bool used;
    char name[VAR_NAME_MAX + 1];
    char value[VAR_VALUE_MAX + 1];
};

static struct variable vars[MAX_VARS];
static int depth;

static bool name_ok(const char *s)
{
    if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z') || s[0] == '_')) {
        return false;
    }
    size_t i = 0;
    for (; s[i] != '\0'; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return i <= VAR_NAME_MAX;
}

static void copy_into(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static bool same(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int script_set(const char *name, const char *value)
{
    if (!name_ok(name)) {
        return -1;
    }
    for (int i = 0; i < MAX_VARS; i++) {
        if (vars[i].used && same(vars[i].name, name)) {
            copy_into(vars[i].value, value, sizeof(vars[i].value));
            return 0;
        }
    }
    for (int i = 0; i < MAX_VARS; i++) {
        if (!vars[i].used) {
            vars[i].used = true;
            copy_into(vars[i].name, name, sizeof(vars[i].name));
            copy_into(vars[i].value, value, sizeof(vars[i].value));
            return 0;
        }
    }
    return -1;
}

const char *script_get(const char *name)
{
    for (int i = 0; i < MAX_VARS; i++) {
        if (vars[i].used && same(vars[i].name, name)) {
            return vars[i].value;
        }
    }
    return "";
}

static bool is_name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

void script_expand(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    size_t i = 0;
    while (in[i] != '\0' && o + 1 < cap) {
        if (in[i] == '$' && is_name_char(in[i + 1]) && !(in[i + 1] >= '0' && in[i + 1] <= '9')) {
            char name[VAR_NAME_MAX + 1];
            size_t n = 0;
            i++;
            while (is_name_char(in[i]) && n < VAR_NAME_MAX) {
                name[n++] = in[i++];
            }
            name[n] = '\0';
            const char *v = script_get(name);
            for (size_t k = 0; v[k] != '\0' && o + 1 < cap; k++) {
                out[o++] = v[k];
            }
            continue;
        }
        out[o++] = in[i++];
    }
    out[o] = '\0';
}

/* Executa uma linha já lida: expande variáveis, separa argumentos e despacha. */
static void run_line(char *line)
{
    char expanded[LINE_MAX];
    char *argv[MAX_ARGS];
    script_expand(line, expanded, sizeof(expanded));
    int argc = parse_line(expanded, argv, MAX_ARGS);
    if (argc < 0) {
        kprintf("sh: erro de sintaxe na linha: %s\n", line);
        return;
    }
    if (argc > 0) {
        commands_dispatch(argc, argv);
    }
}

int script_run_file(const char *path)
{
    struct vfs_stat st;
    if (vfs_stat(path, &st) != VFS_OK || st.is_dir) {
        kprintf("sh: nao foi possivel ler '%s'\n", path);
        return -1;
    }
    if (depth >= MAX_DEPTH) {
        kprintf("sh: scripts aninhados demais (limite %u)\n", (unsigned)MAX_DEPTH);
        return -1;
    }

    uint8_t *buf = kmalloc(st.size + 1);
    if (buf == NULL) {
        kprintf("sh: memoria esgotada\n");
        return -1;
    }
    size_t got = 0;
    if (vfs_read(path, 0, buf, st.size, &got) != VFS_OK) {
        kfree(buf);
        kprintf("sh: erro ao ler '%s'\n", path);
        return -1;
    }
    buf[got] = '\0';

    depth++;
    char line[LINE_MAX];
    size_t n = 0;
    for (size_t i = 0; i <= got; i++) {
        char c = (i < got) ? (char)buf[i] : '\n';
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            if (n + 1 < sizeof(line)) {
                line[n++] = c;
            }
            continue;
        }
        line[n] = '\0';
        size_t start = 0;
        while (line[start] == ' ' || line[start] == '\t') {
            start++;
        }
        if (line[start] != '\0' && line[start] != '#') {
            run_line(line + start);
        }
        n = 0;
    }
    depth--;
    kfree(buf);
    return 0;
}
