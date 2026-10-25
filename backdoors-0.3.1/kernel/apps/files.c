/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apps/editor.h"
#include "apps/files.h"
#include "apps/ui.h"
#include "drivers/keyboard.h"
#include "drivers/vga_text.h"
#include "fs/vfs.h"

/*
 * Gerenciador de arquivos: lista o diretório atual, navega com setas e cria,
 * renomeia, apaga (com confirmação), copia e move. Arquivos abrem no editor.
 * Diretórios só são apagados se estiverem vazios (como rmdir).
 */

#define FM_MAX_ENTRIES 128
#define FM_LIST_ROWS   22          /* linhas 1..22 */
#define FM_HEADER_ROW  0
#define FM_STATUS_ROW  23
#define FM_HELP_ROW    24

struct fm_entry {
    char name[VFS_NAME_MAX + 1];
    bool is_dir;
    size_t size;
};

static struct fm_entry entries[FM_MAX_ENTRIES];
static int nentries;
static int sel;
static int first;
static char cwd[VFS_PATH_MAX + 1];
static char status[80];
static uint8_t status_attr;
static bool has_status;

static void set_status(const char *text, uint8_t attr)
{
    size_t i = 0;
    while (text[i] != '\0' && i + 1 < sizeof(status)) {
        status[i] = text[i];
        i++;
    }
    status[i] = '\0';
    status_attr = attr;
    has_status = true;
}

static bool streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static bool collect_cb(const char *name, bool is_dir, size_t size, void *ctx)
{
    (void)ctx;
    if (nentries < FM_MAX_ENTRIES) {
        struct fm_entry *e = &entries[nentries++];
        size_t i = 0;
        while (name[i] != '\0' && i < VFS_NAME_MAX) {
            e->name[i] = name[i];
            i++;
        }
        e->name[i] = '\0';
        e->is_dir = is_dir;
        e->size = size;
    }
    return true;
}

/* Recarrega o diretório atual: ".." primeiro (se não for a raiz), depois pastas, depois arquivos. */
static void refresh(void)
{
    if (vfs_getcwd(cwd, sizeof(cwd)) != VFS_OK) {
        cwd[0] = '?';
        cwd[1] = '\0';
    }
    static struct fm_entry tmp[FM_MAX_ENTRIES];
    int n = 0;
    nentries = 0;
    vfs_list(".", collect_cb, NULL);

    if (!streq(cwd, "/")) {
        tmp[n].name[0] = '.';
        tmp[n].name[1] = '.';
        tmp[n].name[2] = '\0';
        tmp[n].is_dir = true;
        tmp[n].size = 0;
        n++;
    }
    for (int pass = 0; pass < 2; pass++) {
        bool want_dir = (pass == 0);
        for (int i = 0; i < nentries; i++) {
            if (entries[i].is_dir == want_dir && n < FM_MAX_ENTRIES) {
                tmp[n++] = entries[i];
            }
        }
    }
    for (int i = 0; i < n; i++) {
        entries[i] = tmp[i];
    }
    nentries = n;
    if (sel >= nentries) {
        sel = nentries - 1;
    }
    if (sel < 0) {
        sel = 0;
    }
}

static void select_name(const char *name)
{
    for (int i = 0; i < nentries; i++) {
        if (streq(entries[i].name, name)) {
            sel = i;
            return;
        }
    }
}

static void keep_visible(void)
{
    if (sel < first) {
        first = sel;
    }
    if (sel >= first + FM_LIST_ROWS) {
        first = sel - FM_LIST_ROWS + 1;
    }
    if (first < 0) {
        first = 0;
    }
}

static void put_number(char *buf, size_t cap, size_t value, const char *suffix)
{
    char digits[24];
    int d = 0;
    do {
        digits[d++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && d < 20);
    size_t n = 0;
    while (d > 0 && n + 1 < cap) {
        buf[n++] = digits[--d];
    }
    for (size_t i = 0; suffix[i] != '\0' && n + 1 < cap; i++) {
        buf[n++] = suffix[i];
    }
    buf[n] = '\0';
}

static void draw(void)
{
    keep_visible();
    ui_clear(UI_ATTR_NORMAL);

    ui_fill_row(FM_HEADER_ROW, UI_ATTR_INVERSE);
    ui_text(FM_HEADER_ROW, 1, "Arquivos:", UI_ATTR_INVERSE, 10);
    ui_text(FM_HEADER_ROW, 12, cwd, UI_ATTR_INVERSE, 66);

    if (nentries == 0) {
        ui_text(1, 2, "(diretorio vazio)", UI_ATTR_DIM, 40);
    }
    for (int r = 0; r < FM_LIST_ROWS; r++) {
        int idx = first + r;
        if (idx >= nentries) {
            break;
        }
        const struct fm_entry *e = &entries[idx];
        bool selected = (idx == sel);
        uint8_t attr = selected ? UI_ATTR_INVERSE : (e->is_dir ? 0x0B : UI_ATTR_NORMAL);
        unsigned row = (unsigned)(r + 1);
        ui_fill_row(row, attr);
        char label[VFS_NAME_MAX + 4];
        size_t n = 0;
        for (size_t i = 0; e->name[i] != '\0' && n + 2 < sizeof(label); i++) {
            label[n++] = e->name[i];
        }
        if (e->is_dir) {
            label[n++] = '/';
        }
        label[n] = '\0';
        ui_text(row, 2, label, attr, 40);
        if (!e->is_dir) {
            char size[24];
            put_number(size, sizeof(size), e->size, " bytes");
            ui_text(row, 58, size, attr, 20);
        }
    }

    ui_fill_row(FM_STATUS_ROW, UI_ATTR_NORMAL);
    if (has_status) {
        ui_text(FM_STATUS_ROW, 1, status, status_attr, 78);
    } else {
        ui_text(FM_STATUS_ROW, 1,
                "Enter abrir  Bksp subir  n novo arq  p nova pasta  e editar", UI_ATTR_DIM, 78);
    }
    ui_text(FM_HELP_ROW, 1,
            "r renomear  d apagar  c copiar  m mover  q sair", UI_ATTR_DIM, 78);
    vga_set_cursor((unsigned)(sel - first + 1), 1);
}

static void act_open(void)
{
    if (nentries == 0) {
        return;
    }
    struct fm_entry *e = &entries[sel];
    if (e->is_dir) {
        if (vfs_chdir(e->name) != VFS_OK) {
            set_status("nao foi possivel abrir a pasta", UI_ATTR_ERROR);
        }
        sel = 0;
        first = 0;
        has_status = false;
        return;
    }
    editor_run(e->name);
    has_status = false;
}

static void act_up(void)
{
    if (vfs_chdir("..") == VFS_OK) {
        sel = 0;
        first = 0;
        has_status = false;
    }
}

/* Lê um nome; devolve 0 se o usuário cancelou ou deixou vazio. */
static int read_name(const char *label, char *buf, size_t cap)
{
    if (!ui_prompt(FM_STATUS_ROW, label, buf, cap)) {
        return 0;
    }
    return buf[0] != '\0';
}

static void act_new(bool dir)
{
    char name[VFS_NAME_MAX + 1];
    if (!read_name(dir ? "Nome da nova pasta: " : "Nome do novo arquivo: ", name, sizeof(name))) {
        has_status = false;
        return;
    }
    int st = dir ? vfs_mkdir(name) : vfs_touch(name);
    if (st != VFS_OK) {
        set_status(vfs_strerror(st), UI_ATTR_ERROR);
    } else {
        set_status(dir ? "pasta criada" : "arquivo criado", UI_ATTR_OK);
        refresh();
        select_name(name);
    }
}

static bool selected_is_up(void)
{
    return nentries > 0 && streq(entries[sel].name, "..");
}

static void act_rename(void)
{
    if (nentries == 0 || selected_is_up()) {
        return;
    }
    char name[VFS_NAME_MAX + 1];
    if (!read_name("Renomear para: ", name, sizeof(name))) {
        has_status = false;
        return;
    }
    int st = vfs_rename(entries[sel].name, name);
    if (st != VFS_OK) {
        set_status(vfs_strerror(st), UI_ATTR_ERROR);
        return;
    }
    refresh();
    select_name(name);
    set_status("renomeado", UI_ATTR_OK);
}

static void act_delete(void)
{
    if (nentries == 0 || selected_is_up()) {
        return;
    }
    char answer[4];
    char label[VFS_NAME_MAX + 16];
    size_t n = 0;
    const char *pre = "Apagar ";
    for (size_t i = 0; pre[i] != '\0'; i++) {
        label[n++] = pre[i];
    }
    for (size_t i = 0; entries[sel].name[i] != '\0' && n + 12 < sizeof(label); i++) {
        label[n++] = entries[sel].name[i];
    }
    const char *post = "? (s/n): ";
    for (size_t i = 0; post[i] != '\0' && n + 1 < sizeof(label); i++) {
        label[n++] = post[i];
    }
    label[n] = '\0';

    if (!ui_prompt(FM_STATUS_ROW, label, answer, sizeof(answer)) ||
        (answer[0] != 's' && answer[0] != 'S')) {
        set_status("cancelado", UI_ATTR_NORMAL);
        return;
    }
    int st = entries[sel].is_dir ? vfs_rmdir(entries[sel].name) : vfs_remove(entries[sel].name);
    if (st != VFS_OK) {
        set_status(vfs_strerror(st), UI_ATTR_ERROR);
    } else {
        set_status("apagado", UI_ATTR_OK);
    }
    refresh();
}

static void act_copy_or_move(bool move)
{
    if (nentries == 0 || selected_is_up()) {
        return;
    }
    char dst[VFS_PATH_MAX + 1];
    if (!read_name(move ? "Mover para: " : "Copiar para: ", dst, sizeof(dst))) {
        has_status = false;
        return;
    }
    int st = move ? vfs_rename(entries[sel].name, dst) : vfs_copy(entries[sel].name, dst);
    if (st != VFS_OK) {
        set_status(vfs_strerror(st), UI_ATTR_ERROR);
    } else {
        set_status(move ? "movido" : "copiado", UI_ATTR_OK);
    }
    refresh();
}

void files_run(void)
{
    sel = 0;
    first = 0;
    has_status = false;
    refresh();
    vga_show_cursor(1);

    bool quit = false;
    while (!quit) {
        draw();
        int key = keyboard_getkey();
        switch (key) {
        case 'q': case KEY_ESC:
            quit = true;
            break;
        case KEY_UP: case 'k':
            if (sel > 0) sel--;
            has_status = false;
            break;
        case KEY_DOWN: case 'j':
            if (sel + 1 < nentries) sel++;
            has_status = false;
            break;
        case KEY_PGUP:
            sel -= 10;
            if (sel < 0) sel = 0;
            break;
        case KEY_PGDN:
            sel += 10;
            if (sel >= nentries) sel = nentries - 1;
            break;
        case KEY_HOME:
            sel = 0;
            break;
        case KEY_END:
            sel = nentries - 1;
            break;
        case '\n':
            act_open();
            refresh();
            break;
        case '\b': case KEY_LEFT:
            act_up();
            refresh();
            break;
        case 'e':
            if (nentries > 0 && !entries[sel].is_dir) {
                editor_run(entries[sel].name);
                has_status = false;
                refresh();
            } else {
                set_status("selecione um arquivo para editar", UI_ATTR_ERROR);
            }
            break;
        case 'n':
            act_new(false);
            break;
        case 'p':
            act_new(true);
            break;
        case 'r':
            act_rename();
            break;
        case 'd':
            act_delete();
            break;
        case 'c':
            act_copy_or_move(false);
            break;
        case 'm':
            act_copy_or_move(true);
            break;
        default:
            break;
        }
    }
    vga_show_cursor(0);
    vga_clear();
}
