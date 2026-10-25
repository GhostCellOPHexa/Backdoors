/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fs/vfs.h"
#include "kernel/string.h"
#include "mm/heap.h"

/*
 * Árvore de nós no heap do kernel. Cada diretório guarda a lista de filhos
 * (ligada, na ordem de criação). Arquivos guardam seus bytes em um bloco
 * próprio, sempre do tamanho exato do conteúdo.
 */

struct node {
    char name[VFS_NAME_MAX + 1];
    struct node *parent;
    struct node *first;     /* primeiro filho (só diretórios) */
    struct node *next;      /* irmão seguinte */
    bool is_dir;
    uint8_t *data;          /* conteúdo (só arquivos) */
    size_t size;
};

#define MAX_COMPONENTS 32

static struct node *root;
static struct node *cwd;

const char *vfs_strerror(int status)
{
    switch (status) {
    case VFS_OK:            return "ok";
    case VFS_ENOENT:        return "nao encontrado";
    case VFS_EEXIST:        return "ja existe";
    case VFS_ENOTDIR:       return "nao e um diretorio";
    case VFS_EISDIR:        return "e um diretorio";
    case VFS_ENOTEMPTY:     return "diretorio nao esta vazio";
    case VFS_EINVAL:        return "nome ou caminho invalido";
    case VFS_ENAMETOOLONG:  return "nome ou caminho longo demais";
    case VFS_EFBIG:         return "arquivo grande demais (limite 64 KiB)";
    case VFS_ENOMEM:        return "memoria esgotada";
    case VFS_EBUSY:         return "diretorio em uso (e o atual ou um ancestral)";
    default:                return "erro desconhecido";
    }
}

/* ---------- utilidades ---------- */

static bool str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static bool copy_path(char *dst, const char *src)
{
    size_t i = 0;
    while (src[i] != '\0') {
        if (i >= VFS_PATH_MAX) {
            return false;
        }
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return true;
}

static void copy_name(char *dst, const char *src)
{
    size_t i = 0;
    while (src[i] != '\0' && i < VFS_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Valida um nome de um único componente. Retorna VFS_OK ou o erro. */
static int check_name(const char *name)
{
    if (name[0] == '\0' || str_eq(name, ".") || str_eq(name, "..")) {
        return VFS_EINVAL;
    }
    size_t len = 0;
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '/' || (unsigned char)*p <= 32 || (unsigned char)*p > 126) {
            return VFS_EINVAL;          /* sem espaço, controle ou '/' */
        }
        len++;
    }
    return (len > VFS_NAME_MAX) ? VFS_ENAMETOOLONG : VFS_OK;
}

/*
 * Divide 'buf' (cópia editável do caminho) em componentes, trocando as barras
 * por '\0'. Retorna a quantidade ou -1 se houver componentes demais.
 */
static int tokenize(char *buf, char **comps, int max)
{
    int n = 0;
    char *p = buf;
    for (;;) {
        while (*p == '/') {
            *p = '\0';
            p++;
        }
        if (*p == '\0') {
            return n;
        }
        if (n >= max) {
            return -1;
        }
        comps[n++] = p;
        while (*p != '\0' && *p != '/') {
            p++;
        }
    }
}

static struct node *find_child(struct node *dir, const char *name)
{
    for (struct node *c = dir->first; c != NULL; c = c->next) {
        if (str_eq(c->name, name)) {
            return c;
        }
    }
    return NULL;
}

static void link_append(struct node *dir, struct node *n)
{
    n->next = NULL;
    if (dir->first == NULL) {
        dir->first = n;
        return;
    }
    struct node *p = dir->first;
    while (p->next != NULL) {
        p = p->next;
    }
    p->next = n;
}

static void unlink_node(struct node *n)
{
    struct node *dir = n->parent;
    if (dir == NULL) {
        return;
    }
    if (dir->first == n) {
        dir->first = n->next;
    } else {
        for (struct node *p = dir->first; p != NULL; p = p->next) {
            if (p->next == n) {
                p->next = n->next;
                break;
            }
        }
    }
    n->next = NULL;
}

static struct node *new_node(struct node *parent, const char *name, bool is_dir)
{
    struct node *n = kmalloc(sizeof(*n));
    if (n == NULL) {
        return NULL;
    }
    memset(n, 0, sizeof(*n));
    copy_name(n->name, name);
    n->is_dir = is_dir;
    n->parent = parent;
    link_append(parent, n);
    return n;
}

static void free_node(struct node *n)
{
    kfree(n->data);
    kfree(n);
}

static bool is_ancestor_or_self(const struct node *a, const struct node *b)
{
    for (const struct node *p = b; p != NULL; p = p->parent) {
        if (p == a) {
            return true;
        }
    }
    return false;
}

/* Substitui o conteúdo de um arquivo (ou acrescenta, se 'append'). */
static int set_data(struct node *n, const void *data, size_t len, bool append)
{
    size_t keep = append ? n->size : 0;
    size_t total = keep + len;
    if (total > VFS_FILE_MAX) {
        return VFS_EFBIG;
    }
    uint8_t *buf = kmalloc(total ? total : 1);
    if (buf == NULL) {
        return VFS_ENOMEM;
    }
    if (keep != 0) {
        memcpy(buf, n->data, keep);
    }
    if (len != 0) {
        memcpy(buf + keep, data, len);
    }
    kfree(n->data);
    n->data = buf;
    n->size = total;
    return VFS_OK;
}

/*
 * Resolve um caminho. Com 'want_parent', resolve só o diretório que contém o
 * último componente e copia esse componente para 'leaf' (VFS_NAME_MAX + 1).
 */
static int resolve(const char *path, bool want_parent, struct node **out, char *leaf)
{
    char buf[VFS_PATH_MAX + 1];
    if (path[0] == '\0') {
        return VFS_EINVAL;
    }
    if (!copy_path(buf, path)) {
        return VFS_ENAMETOOLONG;
    }
    char *comps[MAX_COMPONENTS];
    int n = tokenize(buf, comps, MAX_COMPONENTS);
    if (n < 0) {
        return VFS_ENAMETOOLONG;
    }

    int limit = n;
    if (want_parent) {
        if (n == 0) {
            return VFS_EINVAL;          /* '/' não tem nome para criar */
        }
        int st = check_name(comps[n - 1]);
        if (st != VFS_OK) {
            return st;
        }
        if (leaf != NULL) {
            copy_name(leaf, comps[n - 1]);
        }
        limit = n - 1;
    }

    struct node *cur = (path[0] == '/') ? root : cwd;
    for (int i = 0; i < limit; i++) {
        if (!cur->is_dir) {
            return VFS_ENOTDIR;
        }
        if (str_eq(comps[i], ".")) {
            continue;
        }
        if (str_eq(comps[i], "..")) {
            if (cur->parent != NULL) {
                cur = cur->parent;
            }
            continue;
        }
        struct node *c = find_child(cur, comps[i]);
        if (c == NULL) {
            return VFS_ENOENT;
        }
        cur = c;
    }
    if (want_parent && !cur->is_dir) {
        return VFS_ENOTDIR;
    }
    *out = cur;
    return VFS_OK;
}

/* Caminho absoluto de um nó (usado por vfs_getcwd). */
static int path_of(const struct node *n, char *buf, size_t cap)
{
    if (n->parent == NULL) {
        if (cap < 2) {
            return VFS_ENAMETOOLONG;
        }
        buf[0] = '/';
        buf[1] = '\0';
        return VFS_OK;
    }
    const struct node *chain[MAX_COMPONENTS];
    int depth = 0;
    for (const struct node *p = n; p->parent != NULL; p = p->parent) {
        if (depth >= MAX_COMPONENTS) {
            return VFS_ENAMETOOLONG;
        }
        chain[depth++] = p;
    }
    size_t pos = 0;
    for (int i = depth - 1; i >= 0; i--) {
        if (pos + 1 >= cap) {
            return VFS_ENAMETOOLONG;
        }
        buf[pos++] = '/';
        for (const char *s = chain[i]->name; *s != '\0'; s++) {
            if (pos + 1 >= cap) {
                return VFS_ENAMETOOLONG;
            }
            buf[pos++] = *s;
        }
    }
    buf[pos] = '\0';
    return VFS_OK;
}

/* ---------- inicialização e initramfs ---------- */

void vfs_init(void)
{
    root = kmalloc(sizeof(*root));
    if (root == NULL) {
        return;
    }
    memset(root, 0, sizeof(*root));
    copy_name(root->name, "/");
    root->is_dir = true;
    cwd = root;
}

static struct node *mkdir_all(const char *abs)
{
    char buf[VFS_PATH_MAX + 1];
    if (!copy_path(buf, abs)) {
        return NULL;
    }
    char *comps[MAX_COMPONENTS];
    int n = tokenize(buf, comps, MAX_COMPONENTS);
    if (n < 0) {
        return NULL;
    }
    struct node *cur = root;
    for (int i = 0; i < n; i++) {
        if (str_eq(comps[i], ".")) {
            continue;
        }
        if (check_name(comps[i]) != VFS_OK) {
            return NULL;            /* inclui '..': initramfs não pode sair da raiz */
        }
        struct node *c = find_child(cur, comps[i]);
        if (c == NULL) {
            c = new_node(cur, comps[i], true);
            if (c == NULL) {
                return NULL;
            }
        } else if (!c->is_dir) {
            return NULL;
        }
        cur = c;
    }
    return cur;
}

static int load_entry(const char *name, uint32_t mode, const uint8_t *body, size_t size)
{
    while (name[0] == '.' && name[1] == '/') {
        name += 2;
    }
    while (name[0] == '/') {
        name++;
    }
    if (name[0] == '\0' || str_eq(name, ".")) {
        return 0;
    }

    char path[VFS_PATH_MAX + 1];
    path[0] = '/';
    size_t len = 1;
    for (const char *s = name; *s != '\0'; s++) {
        if (len >= VFS_PATH_MAX) {
            return -1;
        }
        path[len++] = *s;
    }
    path[len] = '\0';

    uint32_t type = mode & 0170000u;
    if (type == 0040000u) {                         /* diretório */
        return mkdir_all(path) != NULL ? 0 : -1;
    }
    if (type != 0100000u) {                         /* outros tipos são ignorados */
        return 0;
    }

    /* Arquivo: separa o diretório pai do nome. */
    size_t slash = len - 1;
    while (slash > 0 && path[slash] != '/') {
        slash--;
    }
    char dirpart[VFS_PATH_MAX + 1];
    for (size_t i = 0; i < slash; i++) {
        dirpart[i] = path[i];
    }
    dirpart[slash == 0 ? 1 : slash] = '\0';
    if (slash == 0) {
        dirpart[0] = '/';
    }
    const char *leaf = path + slash + 1;
    if (check_name(leaf) != VFS_OK) {
        return -1;
    }

    struct node *dir = mkdir_all(dirpart);
    if (dir == NULL) {
        return -1;
    }
    struct node *f = find_child(dir, leaf);
    if (f != NULL && f->is_dir) {
        return -1;
    }
    if (f == NULL) {
        f = new_node(dir, leaf, false);
        if (f == NULL) {
            return -1;
        }
    }
    return set_data(f, body, size, false) == VFS_OK ? 0 : -1;
}

#define CPIO_HDR 110u

static size_t align4(size_t n)
{
    return (n + 3u) & ~(size_t)3u;
}

/* Lê um campo hexadecimal de 8 dígitos do cabeçalho newc. */
static uint32_t cpio_field(const uint8_t *p, bool *ok)
{
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t c = p[i];
        uint32_t d;
        if (c >= '0' && c <= '9') {
            d = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = (uint32_t)(c - 'A' + 10);
        } else {
            *ok = false;
            return 0;
        }
        v = (v << 4) | d;
    }
    return v;
}

static int load_cb(const char *name, uint32_t mode, const uint8_t *body, size_t size, void *ctx)
{
    (void)ctx;
    return load_entry(name, mode, body, size);
}

int vfs_cpio_walk(const uint8_t *data, size_t len, vfs_cpio_fn fn, void *ctx)
{
    size_t off = 0;
    int count = 0;

    for (;;) {
        if (len - off < CPIO_HDR) {
            return -1;
        }
        const uint8_t *h = data + off;
        if (h[0] != '0' || h[1] != '7' || h[2] != '0' || h[3] != '7' ||
            h[4] != '0' || h[5] != '1') {
            return -1;                  /* magic "070701" (newc) */
        }
        bool ok = true;
        uint32_t mode = cpio_field(h + 14, &ok);
        uint32_t filesize = cpio_field(h + 54, &ok);
        uint32_t namesize = cpio_field(h + 94, &ok);
        if (!ok || namesize == 0 || namesize > 4096u) {
            return -1;
        }

        size_t name_off = off + CPIO_HDR;
        if (namesize > len - name_off) {
            return -1;
        }
        const char *name = (const char *)(data + name_off);
        if (name[namesize - 1] != '\0') {
            return -1;
        }
        if (str_eq(name, "TRAILER!!!")) {
            return count;
        }

        size_t body_off = align4(name_off + namesize);
        if (body_off > len || filesize > len - body_off) {
            return -1;
        }
        if (fn(name, mode, data + body_off, filesize, ctx) != 0) {
            return -1;
        }
        count++;

        size_t next = align4(body_off + filesize);
        if (next > len) {
            return -1;
        }
        off = next;
    }
}

int vfs_load_cpio(const uint8_t *data, size_t len)
{
    return vfs_cpio_walk(data, len, load_cb, NULL);
}

/* ---------- operações públicas ---------- */

int vfs_stat(const char *path, struct vfs_stat *st)
{
    struct node *n;
    int s = resolve(path, false, &n, NULL);
    if (s != VFS_OK) {
        return s;
    }
    copy_name(st->name, n->name);
    st->is_dir = n->is_dir;
    st->size = n->size;
    return VFS_OK;
}

int vfs_list(const char *path, vfs_list_fn fn, void *ctx)
{
    struct node *n;
    int s = resolve(path, false, &n, NULL);
    if (s != VFS_OK) {
        return s;
    }
    if (!n->is_dir) {
        return VFS_ENOTDIR;
    }
    for (struct node *c = n->first; c != NULL; c = c->next) {
        if (!fn(c->name, c->is_dir, c->size, ctx)) {
            break;
        }
    }
    return VFS_OK;
}

int vfs_read(const char *path, size_t offset, void *buf, size_t n, size_t *got)
{
    struct node *f;
    int s = resolve(path, false, &f, NULL);
    if (s != VFS_OK) {
        return s;
    }
    if (f->is_dir) {
        return VFS_EISDIR;
    }
    size_t avail = (offset < f->size) ? f->size - offset : 0;
    size_t m = (n < avail) ? n : avail;
    if (m != 0) {
        memcpy(buf, f->data + offset, m);
    }
    *got = m;
    return VFS_OK;
}

int vfs_mkdir(const char *path)
{
    struct node *parent;
    char leaf[VFS_NAME_MAX + 1];
    int s = resolve(path, true, &parent, leaf);
    if (s != VFS_OK) {
        return s;
    }
    if (find_child(parent, leaf) != NULL) {
        return VFS_EEXIST;
    }
    return new_node(parent, leaf, true) != NULL ? VFS_OK : VFS_ENOMEM;
}

int vfs_touch(const char *path)
{
    struct node *parent;
    char leaf[VFS_NAME_MAX + 1];
    int s = resolve(path, true, &parent, leaf);
    if (s != VFS_OK) {
        return s;
    }
    struct node *c = find_child(parent, leaf);
    if (c != NULL) {
        return c->is_dir ? VFS_EISDIR : VFS_OK;
    }
    return new_node(parent, leaf, false) != NULL ? VFS_OK : VFS_ENOMEM;
}

/* Escreve ou acrescenta conteúdo; cria o arquivo se não existir. */
static int write_common(const char *path, const void *data, size_t len, bool append)
{
    struct node *parent;
    char leaf[VFS_NAME_MAX + 1];
    int s = resolve(path, true, &parent, leaf);
    if (s != VFS_OK) {
        return s;
    }
    struct node *f = find_child(parent, leaf);
    if (f != NULL && f->is_dir) {
        return VFS_EISDIR;
    }
    bool created = false;
    if (f == NULL) {
        f = new_node(parent, leaf, false);
        if (f == NULL) {
            return VFS_ENOMEM;
        }
        created = true;
    }
    s = set_data(f, data, len, append);
    if (s != VFS_OK && created) {
        unlink_node(f);
        free_node(f);
    }
    return s;
}

int vfs_write_file(const char *path, const void *data, size_t len)
{
    return write_common(path, data, len, false);
}

int vfs_append_file(const char *path, const void *data, size_t len)
{
    return write_common(path, data, len, true);
}

int vfs_remove(const char *path)
{
    struct node *parent;
    char leaf[VFS_NAME_MAX + 1];
    int s = resolve(path, true, &parent, leaf);
    if (s != VFS_OK) {
        return s;
    }
    struct node *c = find_child(parent, leaf);
    if (c == NULL) {
        return VFS_ENOENT;
    }
    if (c->is_dir) {
        return VFS_EISDIR;
    }
    unlink_node(c);
    free_node(c);
    return VFS_OK;
}

int vfs_rmdir(const char *path)
{
    struct node *parent;
    char leaf[VFS_NAME_MAX + 1];
    int s = resolve(path, true, &parent, leaf);
    if (s != VFS_OK) {
        return s;
    }
    struct node *c = find_child(parent, leaf);
    if (c == NULL) {
        return VFS_ENOENT;
    }
    if (!c->is_dir) {
        return VFS_ENOTDIR;
    }
    if (c->first != NULL) {
        return VFS_ENOTEMPTY;
    }
    if (is_ancestor_or_self(c, cwd)) {
        return VFS_EBUSY;
    }
    unlink_node(c);
    free_node(c);
    return VFS_OK;
}

int vfs_rename(const char *src, const char *dst)
{
    struct node *sp;
    char sleaf[VFS_NAME_MAX + 1];
    int s = resolve(src, true, &sp, sleaf);
    if (s != VFS_OK) {
        return s;
    }
    struct node *node = find_child(sp, sleaf);
    if (node == NULL) {
        return VFS_ENOENT;
    }

    /* Destino que já é um diretório: move para dentro dele, mantendo o nome. */
    struct node *d;
    if (resolve(dst, false, &d, NULL) == VFS_OK && d->is_dir) {
        if (is_ancestor_or_self(node, d)) {
            return VFS_EINVAL;
        }
        if (find_child(d, node->name) != NULL) {
            return VFS_EEXIST;
        }
        unlink_node(node);
        node->parent = d;
        link_append(d, node);
        return VFS_OK;
    }

    /* Destino é um nome novo ou um arquivo existente a substituir. */
    struct node *dp;
    char dleaf[VFS_NAME_MAX + 1];
    s = resolve(dst, true, &dp, dleaf);
    if (s != VFS_OK) {
        return s;
    }
    struct node *e = find_child(dp, dleaf);
    if (e == node) {
        return VFS_OK;
    }
    if (e != NULL) {
        if (node->is_dir) {
            return VFS_ENOTDIR;
        }
        unlink_node(e);
        free_node(e);
    }
    if (node->is_dir && is_ancestor_or_self(node, dp)) {
        return VFS_EINVAL;
    }
    unlink_node(node);
    copy_name(node->name, dleaf);
    node->parent = dp;
    link_append(dp, node);
    return VFS_OK;
}

int vfs_copy(const char *src, const char *dst)
{
    struct node *s_node;
    int s = resolve(src, false, &s_node, NULL);
    if (s != VFS_OK) {
        return s;
    }
    if (s_node->is_dir) {
        return VFS_EISDIR;
    }

    struct node *dp;
    char dleaf[VFS_NAME_MAX + 1];
    struct node *d;
    if (resolve(dst, false, &d, NULL) == VFS_OK && d->is_dir) {
        dp = d;
        copy_name(dleaf, s_node->name);
    } else {
        s = resolve(dst, true, &dp, dleaf);
        if (s != VFS_OK) {
            return s;
        }
    }

    struct node *e = find_child(dp, dleaf);
    if (e == s_node) {
        return VFS_OK;
    }
    if (e != NULL && e->is_dir) {
        return VFS_EISDIR;
    }
    if (e != NULL) {
        return set_data(e, s_node->data, s_node->size, false);
    }
    struct node *n = new_node(dp, dleaf, false);
    if (n == NULL) {
        return VFS_ENOMEM;
    }
    s = set_data(n, s_node->data, s_node->size, false);
    if (s != VFS_OK) {
        unlink_node(n);
        free_node(n);
    }
    return s;
}

int vfs_chdir(const char *path)
{
    struct node *n;
    int s = resolve(path, false, &n, NULL);
    if (s != VFS_OK) {
        return s;
    }
    if (!n->is_dir) {
        return VFS_ENOTDIR;
    }
    cwd = n;
    return VFS_OK;
}

int vfs_getcwd(char *buf, size_t cap)
{
    return path_of(cwd, buf, cap);
}
