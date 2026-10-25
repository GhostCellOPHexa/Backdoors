/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fs/vfs.h"
#include "kernel/log.h"
#include "shell/builtins.h"

/* Comandos de arquivos e diretórios sobre o VFS. Erros dizem o caminho e o motivo. */

#define TREE_MAX_DEPTH 12

static void report(const char *cmd, const char *path, int status)
{
    kprintf("%s: '%s': %s\n", cmd, path, vfs_strerror(status));
}

static void usage(const char *text)
{
    kprintf("uso: %s\n", text);
}

/* Junta 'dir' e 'name' em um caminho absoluto ou relativo. Falso se não couber. */
static bool join_path(char *dst, size_t cap, const char *dir, const char *name)
{
    size_t n = 0;
    for (size_t i = 0; dir[i] != '\0'; i++) {
        if (n + 1 >= cap) {
            return false;
        }
        dst[n++] = dir[i];
    }
    if (n == 0 || dst[n - 1] != '/') {
        if (n + 1 >= cap) {
            return false;
        }
        dst[n++] = '/';
    }
    for (size_t i = 0; name[i] != '\0'; i++) {
        if (n + 1 >= cap) {
            return false;
        }
        dst[n++] = name[i];
    }
    dst[n] = '\0';
    return true;
}

/* ---------- ls ---------- */

struct ls_ctx {
    bool long_format;
    int count;
};

static bool ls_cb(const char *name, bool is_dir, size_t size, void *vctx)
{
    struct ls_ctx *c = vctx;
    if (c->long_format) {
        kprintf("%c %8u  %s%s\n", is_dir ? 'd' : '-', (unsigned)size, name, is_dir ? "/" : "");
    } else {
        kprintf("%s%s  ", name, is_dir ? "/" : "");
    }
    c->count++;
    return true;
}

void cmd_ls(int argc, char **argv)
{
    struct ls_ctx c = { false, 0 };
    int i = 1;
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'l' && argv[1][2] == '\0') {
        c.long_format = true;
        i = 2;
    }
    const char *path = (i < argc) ? argv[i] : ".";
    int st = vfs_list(path, ls_cb, &c);
    if (st != VFS_OK) {
        report("ls", path, st);
        return;
    }
    if (!c.long_format && c.count > 0) {
        kprintf("\n");
    }
    if (c.count == 0) {
        kprintf("(vazio)\n");
    }
}

/* ---------- cd / pwd ---------- */

void cmd_cd(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/";
    int st = vfs_chdir(path);
    if (st != VFS_OK) {
        report("cd", path, st);
    }
}

void cmd_pwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char buf[VFS_PATH_MAX + 1];
    if (vfs_getcwd(buf, sizeof(buf)) != VFS_OK) {
        kprintf("pwd: caminho longo demais\n");
        return;
    }
    kprintf("%s\n", buf);
}

/* ---------- criação ---------- */

void cmd_mkdir(int argc, char **argv)
{
    if (argc < 2) {
        usage("mkdir NOME...");
        return;
    }
    for (int i = 1; i < argc; i++) {
        int st = vfs_mkdir(argv[i]);
        if (st != VFS_OK) {
            report("mkdir", argv[i], st);
        }
    }
}

void cmd_touch(int argc, char **argv)
{
    if (argc < 2) {
        usage("touch ARQUIVO...");
        return;
    }
    for (int i = 1; i < argc; i++) {
        int st = vfs_touch(argv[i]);
        if (st != VFS_OK) {
            report("touch", argv[i], st);
        }
    }
}

/* ---------- leitura e escrita ---------- */

void cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        usage("cat ARQUIVO...");
        return;
    }
    for (int i = 1; i < argc; i++) {
        char buf[256];
        size_t off = 0;
        for (;;) {
            size_t got = 0;
            int st = vfs_read(argv[i], off, buf, sizeof(buf), &got);
            if (st != VFS_OK) {
                report("cat", argv[i], st);
                break;
            }
            if (got == 0) {
                break;
            }
            kwrite(buf, got);
            off += got;
        }
    }
}

/* write NOME TEXTO... (sobrescreve, com quebra de linha no fim). */
void cmd_write(int argc, char **argv)
{
    if (argc < 3) {
        usage("write ARQUIVO TEXTO...");
        return;
    }
    int st = vfs_write_file(argv[1], "", 0);
    if (st != VFS_OK) {
        report("write", argv[1], st);
        return;
    }
    for (int i = 2; i < argc; i++) {
        const char *piece = argv[i];
        size_t len = 0;
        while (piece[len] != '\0') {
            len++;
        }
        const char *sep = (i + 1 < argc) ? " " : "\n";
        if (vfs_append_file(argv[1], piece, len) != VFS_OK ||
            vfs_append_file(argv[1], sep, 1) != VFS_OK) {
            report("write", argv[1], VFS_ENOMEM);
            return;
        }
    }
}

/* append NOME TEXTO... (acrescenta ao fim, com quebra de linha). */
void cmd_append(int argc, char **argv)
{
    if (argc < 3) {
        usage("append ARQUIVO TEXTO...");
        return;
    }
    for (int i = 2; i < argc; i++) {
        const char *piece = argv[i];
        size_t len = 0;
        while (piece[len] != '\0') {
            len++;
        }
        const char *sep = (i + 1 < argc) ? " " : "\n";
        int st = vfs_append_file(argv[1], piece, len);
        if (st == VFS_OK) {
            st = vfs_append_file(argv[1], sep, 1);
        }
        if (st != VFS_OK) {
            report("append", argv[1], st);
            return;
        }
    }
}

/* ---------- remoção, cópia e movimento ---------- */

void cmd_rm(int argc, char **argv)
{
    if (argc < 2) {
        usage("rm ARQUIVO...");
        return;
    }
    for (int i = 1; i < argc; i++) {
        struct vfs_stat st;
        int s = vfs_stat(argv[i], &st);
        if (s == VFS_OK && st.is_dir) {
            kprintf("rm: '%s' e um diretorio (use rmdir para diretorios vazios)\n", argv[i]);
            continue;
        }
        s = vfs_remove(argv[i]);
        if (s != VFS_OK) {
            report("rm", argv[i], s);
        }
    }
}

void cmd_rmdir(int argc, char **argv)
{
    if (argc < 2) {
        usage("rmdir DIRETORIO...");
        return;
    }
    for (int i = 1; i < argc; i++) {
        int st = vfs_rmdir(argv[i]);
        if (st != VFS_OK) {
            report("rmdir", argv[i], st);
        }
    }
}

void cmd_cp(int argc, char **argv)
{
    if (argc != 3) {
        usage("cp ORIGEM DESTINO");
        return;
    }
    int st = vfs_copy(argv[1], argv[2]);
    if (st != VFS_OK) {
        report("cp", argv[1], st);
    }
}

void cmd_mv(int argc, char **argv)
{
    if (argc != 3) {
        usage("mv ORIGEM DESTINO");
        return;
    }
    int st = vfs_rename(argv[1], argv[2]);
    if (st != VFS_OK) {
        report("mv", argv[1], st);
    }
}

/* ---------- tree ---------- */

struct tree_ctx {
    const char *path;
    const char *prefix;
    int depth;
};

static void tree_dir(const char *path, const char *prefix, int depth);

static bool tree_cb(const char *name, bool is_dir, size_t size, void *vctx)
{
    struct tree_ctx *c = vctx;
    (void)size;
    kprintf("%s|-- %s%s\n", c->prefix, name, is_dir ? "/" : "");
    if (is_dir) {
        char child[VFS_PATH_MAX + 1];
        char nprefix[64];
        if (!join_path(child, sizeof(child), c->path, name)) {
            return true;
        }
        size_t n = 0;
        for (size_t i = 0; c->prefix[i] != '\0' && n + 5 < sizeof(nprefix); i++) {
            nprefix[n++] = c->prefix[i];
        }
        const char *step = "|   ";
        for (size_t i = 0; step[i] != '\0'; i++) {
            nprefix[n++] = step[i];
        }
        nprefix[n] = '\0';
        tree_dir(child, nprefix, c->depth + 1);
    }
    return true;
}

static void tree_dir(const char *path, const char *prefix, int depth)
{
    if (depth > TREE_MAX_DEPTH) {
        kprintf("%s|-- ...\n", prefix);
        return;
    }
    struct tree_ctx c = { path, prefix, depth };
    vfs_list(path, tree_cb, &c);
}

void cmd_tree(int argc, char **argv)
{
    char cwd[VFS_PATH_MAX + 1];
    const char *path = (argc > 1) ? argv[1] : NULL;
    if (path == NULL) {
        if (vfs_getcwd(cwd, sizeof(cwd)) != VFS_OK) {
            kprintf("tree: caminho longo demais\n");
            return;
        }
        path = cwd;
    }
    struct vfs_stat st;
    int s = vfs_stat(path, &st);
    if (s != VFS_OK) {
        report("tree", path, s);
        return;
    }
    if (!st.is_dir) {
        report("tree", path, VFS_ENOTDIR);
        return;
    }
    kprintf("%s\n", path);
    tree_dir(path, "", 1);
}
