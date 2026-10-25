/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fs/vfs.h"
#include "kernel/log.h"
#include "mm/heap.h"
#include "pkg/pkg.h"

#define PKG_NAME_MAX   24
#define PKG_VERSION_MAX 23
#define INFO_MAX       512
#define RECORD_MAX     16384
#define PKG_DB         "/var/lib/pkg"
#define PKG_MAX_NAMES  32
#define PATH_BUF       (VFS_PATH_MAX + 1)

struct pkg_meta {
    char name[PKG_NAME_MAX + 1];
    char version[PKG_VERSION_MAX + 1];
};

/* ---------- utilidades ---------- */

static bool streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static bool prefix_is(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
        if (*s != *prefix) {
            return false;
        }
        s++;
        prefix++;
    }
    return true;
}

static bool valid_name(const char *s)
{
    size_t n = 0;
    for (; s[n] != '\0'; n++) {
        char c = s[n];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return n >= 1 && n <= PKG_NAME_MAX;
}

static bool valid_version(const char *s)
{
    size_t n = 0;
    for (; s[n] != '\0'; n++) {
        char c = s[n];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return n >= 1 && n <= PKG_VERSION_MAX;
}

/* Nome de entrada cpio sem "./" e sem "/" iniciais. */
static const char *normalize(const char *name)
{
    while (prefix_is(name, "./")) {
        name += 2;
    }
    while (*name == '/') {
        name++;
    }
    return name;
}

static bool join_root(char *dst, size_t cap, const char *rel)
{
    size_t n = 0;
    dst[n++] = '/';
    for (size_t i = 0; rel[i] != '\0'; i++) {
        if (n + 1 >= cap) {
            return false;
        }
        dst[n++] = rel[i];
    }
    dst[n] = '\0';
    return true;
}

/* Cria todos os diretórios de 'path' (exceto o último componente se 'leaf_too' for falso). */
static int ensure_dirs(const char *path, bool leaf_too)
{
    char prefix[PATH_BUF];
    size_t n = 0;
    prefix[0] = '\0';
    for (size_t i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/' && i > 0) {
            prefix[n] = '\0';
            int st = vfs_mkdir(prefix);
            if (st != VFS_OK && st != VFS_EEXIST) {
                return st;
            }
        }
        if (n + 1 >= sizeof(prefix)) {
            return VFS_ENAMETOOLONG;
        }
        prefix[n++] = path[i];
        prefix[n] = '\0';
    }
    if (leaf_too) {
        int st = vfs_mkdir(prefix);
        if (st != VFS_OK && st != VFS_EEXIST) {
            return st;
        }
    }
    return VFS_OK;
}

static int read_whole(const char *path, uint8_t **buf, size_t *len)
{
    struct vfs_stat st;
    int s = vfs_stat(path, &st);
    if (s != VFS_OK) {
        return s;
    }
    if (st.is_dir) {
        return VFS_EISDIR;
    }
    *buf = kmalloc(st.size ? st.size : 1);
    if (*buf == NULL) {
        return VFS_ENOMEM;
    }
    size_t got = 0;
    s = vfs_read(path, 0, *buf, st.size, &got);
    if (s != VFS_OK) {
        kfree(*buf);
        *buf = NULL;
        return s;
    }
    *len = got;
    return VFS_OK;
}

/* Lê "chave=valor" de PKG/INFO ou do registro. */
static bool field(const uint8_t *text, size_t len, const char *key, char *out, size_t cap)
{
    size_t klen = 0;
    while (key[klen] != '\0') {
        klen++;
    }
    size_t i = 0;
    while (i < len) {
        size_t start = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        size_t line_len = i - start;
        if (line_len > klen && text[start + klen] == '=') {
            bool match = true;
            for (size_t k = 0; k < klen; k++) {
                if (text[start + k] != (uint8_t)key[k]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                size_t n = 0;
                for (size_t k = start + klen + 1; k < i && n + 1 < cap; k++) {
                    out[n++] = (char)text[k];
                }
                out[n] = '\0';
                return true;
            }
        }
        i++;
    }
    return false;
}

/* ---------- leitura do pacote ---------- */

struct info_ctx {
    uint8_t info[INFO_MAX];
    size_t info_len;
    bool found;
    int files;
};

static int info_cb(const char *name, uint32_t mode, const uint8_t *body, size_t size, void *vctx)
{
    struct info_ctx *c = vctx;
    const char *n = normalize(name);
    if (streq(n, "PKG/INFO")) {
        if (size > INFO_MAX) {
            return -1;
        }
        for (size_t i = 0; i < size; i++) {
            c->info[i] = body[i];
        }
        c->info_len = size;
        c->found = true;
    } else if ((mode & 0170000u) == 0100000u) {
        c->files++;
    }
    return 0;
}

/* Lê PKG/INFO do pacote. Preenche 'meta' e conta os arquivos. */
static int read_meta(const uint8_t *pkg, size_t len, struct pkg_meta *meta, int *files)
{
    struct info_ctx c = { {0}, 0, false, 0 };
    if (vfs_cpio_walk(pkg, len, info_cb, &c) < 0) {
        kprintf("pkg: arquivo .bkg malformado\n");
        return -1;
    }
    if (!c.found) {
        kprintf("pkg: arquivo .bkg sem PKG/INFO\n");
        return -1;
    }
    if (!field(c.info, c.info_len, "nome", meta->name, sizeof(meta->name)) ||
        !valid_name(meta->name)) {
        kprintf("pkg: nome de pacote invalido em PKG/INFO\n");
        return -1;
    }
    if (!field(c.info, c.info_len, "versao", meta->version, sizeof(meta->version)) ||
        !valid_version(meta->version)) {
        kprintf("pkg: versao invalida em PKG/INFO\n");
        return -1;
    }
    if (files != NULL) {
        *files = c.files;
    }
    return 0;
}

/* ---------- registro dos instalados ---------- */

static bool record_has_file(const uint8_t *rec, size_t rlen, const char *path)
{
    char line[PATH_BUF + 16];
    size_t i = 0;
    while (i < rlen) {
        size_t n = 0;
        while (i < rlen && rec[i] != '\n' && n + 1 < sizeof(line)) {
            line[n++] = (char)rec[i++];
        }
        while (i < rlen && rec[i] != '\n') {
            i++;
        }
        i++;
        line[n] = '\0';
        if (prefix_is(line, "arquivo=") && streq(line + 8, path)) {
            return true;
        }
    }
    return false;
}

/* Conflito: arquivo já existe no sistema e não pertence ao pacote instalado. */
struct conflict_ctx {
    const uint8_t *rec;
    size_t rlen;
    int conflicts;
};

static int conflict_cb(const char *name, uint32_t mode, const uint8_t *body, size_t size, void *vctx)
{
    (void)body;
    (void)size;
    struct conflict_ctx *c = vctx;
    const char *rel = normalize(name);
    if (rel[0] == '\0' || streq(rel, "PKG/INFO") || (mode & 0170000u) != 0100000u) {
        return 0;
    }
    char path[PATH_BUF];
    if (!join_root(path, sizeof(path), rel)) {
        return -1;
    }
    struct vfs_stat st;
    if (vfs_stat(path, &st) == VFS_OK && !record_has_file(c->rec, c->rlen, path)) {
        kprintf("pkg: conflito: %s ja existe e nao pertence a este pacote\n", path);
        c->conflicts++;
        return -1;
    }
    return 0;
}

struct extract_ctx {
    char *rec;
    size_t rused;
    int files;
    int error;
};

static void rec_append(struct extract_ctx *c, const char *text)
{
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (c->rused + 1 >= RECORD_MAX) {
            c->error = 1;
            return;
        }
        c->rec[c->rused++] = text[i];
    }
    c->rec[c->rused] = '\0';
}

static int extract_cb(const char *name, uint32_t mode, const uint8_t *body, size_t size, void *vctx)
{
    struct extract_ctx *c = vctx;
    const char *rel = normalize(name);
    if (rel[0] == '\0' || streq(rel, "PKG/INFO")) {
        return 0;
    }
    char path[PATH_BUF];
    if (!join_root(path, sizeof(path), rel)) {
        return -1;
    }
    uint32_t type = mode & 0170000u;
    if (type == 0040000u) {
        return ensure_dirs(path, true) == VFS_OK ? 0 : -1;
    }
    if (type != 0100000u) {
        return 0;
    }
    if (ensure_dirs(path, false) != VFS_OK) {
        kprintf("pkg: nao foi possivel criar o diretorio de %s\n", path);
        return -1;
    }
    int st = vfs_write_file(path, body, size);
    if (st != VFS_OK) {
        kprintf("pkg: %s: %s\n", path, vfs_strerror(st));
        return -1;
    }
    char line[PATH_BUF + 16] = "arquivo=";
    size_t n = 8;
    for (size_t i = 0; path[i] != '\0' && n + 2 < sizeof(line); i++) {
        line[n++] = path[i];
    }
    line[n++] = '\n';
    line[n] = '\0';
    rec_append(c, line);
    c->files++;
    return c->error ? -1 : 0;
}

/* Remove os arquivos listados no registro de um pacote instalado. */
static void remove_recorded(const uint8_t *rec, size_t rlen)
{
    size_t i = 0;
    char line[PATH_BUF + 16];
    while (i < rlen) {
        size_t n = 0;
        while (i < rlen && rec[i] != '\n') {
            if (n + 1 < sizeof(line)) {
                line[n++] = (char)rec[i];
            }
            i++;
        }
        i++;
        line[n] = '\0';
        if (prefix_is(line, "arquivo=")) {
            (void)vfs_remove(line + 8);
        }
    }
}

/* Caminho do registro de um pacote: /var/lib/pkg/NOME */
static void record_path(const char *name, char *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; PKG_DB[i] != '\0' && n + 1 < cap; i++) {
        out[n++] = PKG_DB[i];
    }
    out[n++] = '/';
    for (size_t i = 0; name[i] != '\0' && n + 1 < cap; i++) {
        out[n++] = name[i];
    }
    out[n] = '\0';
}

/* ---------- operações públicas ---------- */

int pkg_install(const char *archive)
{
    uint8_t *pkg = NULL;
    size_t plen = 0;
    int rs = read_whole(archive, &pkg, &plen);
    if (rs != VFS_OK) {
        kprintf("pkg: '%s': %s\n", archive, vfs_strerror(rs));
        return -1;
    }

    struct pkg_meta meta;
    int files = 0;
    if (read_meta(pkg, plen, &meta, &files) != 0) {
        kfree(pkg);
        return -1;
    }

    char rpath[PATH_BUF];
    record_path(meta.name, rpath, sizeof(rpath));
    uint8_t *old = NULL;
    size_t olen = 0;
    bool installed = (read_whole(rpath, &old, &olen) == VFS_OK);
    char old_version[PKG_VERSION_MAX + 1] = "";
    if (installed) {
        (void)field(old, olen, "versao", old_version, sizeof(old_version));
        if (streq(old_version, meta.version)) {
            kprintf("pkg: %s %s ja esta instalado\n", meta.name, meta.version);
            kfree(old);
            kfree(pkg);
            return 0;
        }
    }

    /* Verificação antes de qualquer mudança: nenhum arquivo de outro dono é sobrescrito. */
    struct conflict_ctx cc = { old, olen, 0 };
    if (vfs_cpio_walk(pkg, plen, conflict_cb, &cc) < 0) {
        kfree(old);
        kfree(pkg);
        kprintf("pkg: instalacao cancelada, nada foi alterado\n");
        return -1;
    }

    if (installed) {
        remove_recorded(old, olen);
    }

    char *rec = kmalloc(RECORD_MAX);
    if (rec == NULL) {
        kfree(old);
        kfree(pkg);
        kprintf("pkg: memoria esgotada\n");
        return -1;
    }
    rec[0] = '\0';
    struct extract_ctx ec = { rec, 0, 0, 0 };
    rec_append(&ec, "nome=");
    rec_append(&ec, meta.name);
    rec_append(&ec, "\nversao=");
    rec_append(&ec, meta.version);
    rec_append(&ec, "\n");

    int walked = vfs_cpio_walk(pkg, plen, extract_cb, &ec);
    kfree(old);
    kfree(pkg);
    if (walked < 0 || ec.error) {
        kfree(rec);
        kprintf("pkg: falha ao instalar %s (instalacao parcial; use pkg remove %s)\n",
                meta.name, meta.name);
        return -1;
    }

    if (ensure_dirs(rpath, false) != VFS_OK || vfs_write_file(rpath, rec, ec.rused) != VFS_OK) {
        kfree(rec);
        kprintf("pkg: nao foi possivel gravar o registro de %s\n", meta.name);
        return -1;
    }
    kfree(rec);

    if (installed) {
        kprintf("pkg: %s atualizado de %s para %s (%d arquivos)\n",
                meta.name, old_version, meta.version, ec.files);
    } else {
        kprintf("pkg: %s %s instalado (%d arquivos)\n", meta.name, meta.version, ec.files);
    }
    (void)files;
    return 0;
}

int pkg_remove(const char *name)
{
    if (!valid_name(name)) {
        kprintf("pkg: nome invalido: %s\n", name);
        return -1;
    }
    char rpath[PATH_BUF];
    record_path(name, rpath, sizeof(rpath));
    uint8_t *rec = NULL;
    size_t rlen = 0;
    int s = read_whole(rpath, &rec, &rlen);
    if (s != VFS_OK) {
        kprintf("pkg: %s nao esta instalado\n", name);
        return -1;
    }
    remove_recorded(rec, rlen);
    kfree(rec);
    if (vfs_remove(rpath) != VFS_OK) {
        kprintf("pkg: nao foi possivel apagar o registro de %s\n", name);
        return -1;
    }
    kprintf("pkg: %s removido\n", name);
    return 0;
}

struct list_ctx {
    int count;
};

static bool list_cb(const char *name, bool is_dir, size_t size, void *vctx)
{
    (void)size;
    struct list_ctx *c = vctx;
    /* Só entradas com nome de pacote válido são registros; o resto é ignorado. */
    if (is_dir || !valid_name(name)) {
        return true;
    }
    char rpath[PATH_BUF];
    record_path(name, rpath, sizeof(rpath));
    uint8_t *rec = NULL;
    size_t rlen = 0;
    char version[PKG_VERSION_MAX + 1] = "?";
    if (read_whole(rpath, &rec, &rlen) == VFS_OK) {
        (void)field(rec, rlen, "versao", version, sizeof(version));
        kfree(rec);
    }
    kprintf("%s %s\n", name, version);
    c->count++;
    return true;
}

void pkg_list(void)
{
    struct list_ctx c = { 0 };
    if (vfs_list(PKG_DB, list_cb, &c) != VFS_OK || c.count == 0) {
        kprintf("nenhum pacote instalado\n");
    }
}

int pkg_info(const char *archive)
{
    uint8_t *pkg = NULL;
    size_t plen = 0;
    int rs = read_whole(archive, &pkg, &plen);
    if (rs != VFS_OK) {
        kprintf("pkg: '%s': %s\n", archive, vfs_strerror(rs));
        return -1;
    }
    struct pkg_meta meta;
    int files = 0;
    if (read_meta(pkg, plen, &meta, &files) != 0) {
        kfree(pkg);
        return -1;
    }
    char desc[64] = "";
    struct info_ctx c = { {0}, 0, false, 0 };
    (void)vfs_cpio_walk(pkg, plen, info_cb, &c);
    (void)field(c.info, c.info_len, "descricao", desc, sizeof(desc));
    kprintf("nome: %s\nversao: %s\ndescricao: %s\narquivos: %d\n",
            meta.name, meta.version, desc, files);
    kfree(pkg);
    return 0;
}

struct repo_name {
    char name[VFS_NAME_MAX + 1];
};

struct repo_ctx {
    struct repo_name *names;
    int count;
};

static bool repo_cb(const char *name, bool is_dir, size_t size, void *vctx)
{
    (void)size;
    struct repo_ctx *c = vctx;
    if (is_dir || c->count >= PKG_MAX_NAMES) {
        return true;
    }
    size_t n = 0;
    while (name[n] != '\0') {
        n++;
    }
    if (n < 5 || !streq(name + n - 4, ".bkg")) {
        return true;
    }
    size_t i = 0;
    while (name[i] != '\0' && i < VFS_NAME_MAX) {
        c->names[c->count].name[i] = name[i];
        i++;
    }
    c->names[c->count].name[i] = '\0';
    c->count++;
    return true;
}

int pkg_update(const char *dir)
{
    static struct repo_name names[PKG_MAX_NAMES];
    struct repo_ctx c = { names, 0 };
    if (vfs_list(dir, repo_cb, &c) != VFS_OK) {
        kprintf("pkg: repositorio nao encontrado: %s\n", dir);
        return -1;
    }
    if (c.count == 0) {
        kprintf("pkg: nenhum arquivo .bkg em %s\n", dir);
        return 0;
    }

    int errors = 0;
    for (int i = 0; i < c.count; i++) {
        char path[PATH_BUF];
        size_t n = 0;
        for (size_t k = 0; dir[k] != '\0' && n + 2 < sizeof(path); k++) {
            path[n++] = dir[k];
        }
        if (n == 0 || path[n - 1] != '/') {
            path[n++] = '/';
        }
        for (size_t k = 0; c.names[i].name[k] != '\0' && n + 1 < sizeof(path); k++) {
            path[n++] = c.names[i].name[k];
        }
        path[n] = '\0';
        if (pkg_install(path) != 0) {
            errors++;
        }
    }
    kprintf("pkg: repositorio %s: %d pacote(s), %d com erro\n", dir, c.count, errors);
    return errors == 0 ? 0 : -1;
}
