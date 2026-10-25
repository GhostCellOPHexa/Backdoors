/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apps/editor.h"
#include "apps/ui.h"
#include "drivers/keyboard.h"
#include "drivers/vga_text.h"
#include "fs/vfs.h"
#include "kernel/log.h"
#include "lang/int.h"
#include "mm/heap.h"

/*
 * Editor de texto em modo texto, inspirado no vi/Vim.
 *
 * Modos: NORMAL (movimento e comandos), INSERIR (digita o texto) e COMANDO
 * (linha com ':' embaixo). Movimento: h j k l, setas, 0 $ w b gg G, PgUp/PgDn.
 * Edição: i a I A o O x dd yy p P. Comandos: :w :q :q! :wq :x :int :run :help.
 *
 * Limites: 512 linhas de até 119 caracteres; sem desfazer (u) nesta versão.
 */

#define ED_MAX_LINES   512
#define ED_MAX_COLS    120            /* inclui o terminador */
#define ED_TEXT_ROWS   23             /* linhas 0..22: texto */
#define ED_STATUS_ROW  23
#define ED_CMD_ROW     24
#define ED_PAGE        20

enum ed_mode { ED_NORMAL, ED_INSERT, ED_COMMAND };

static char lines[ED_MAX_LINES][ED_MAX_COLS];
static int nlines;
static int cy;                 /* linha do cursor */
static int cx;                 /* coluna do cursor */
static int top;                /* primeira linha visível */
static int left;               /* primeira coluna visível */
static bool modified;
static char path[VFS_PATH_MAX + 1];
static enum ed_mode mode;
static char cmdbuf[80];
static int cmdlen;
static int pending;            /* tecla de comando em duas partes: 'd', 'y', 'g' */
static bool quit;
static char yank[ED_MAX_COLS];
static bool has_yank;
static char msg[80];
static uint8_t msg_attr;

/* ---------- utilidades ---------- */

static int len_of(int y)
{
    int n = 0;
    while (lines[y][n] != '\0') {
        n++;
    }
    return n;
}

static void set_msg(const char *text, uint8_t attr)
{
    size_t i = 0;
    while (text[i] != '\0' && i + 1 < sizeof(msg)) {
        msg[i] = text[i];
        i++;
    }
    msg[i] = '\0';
    msg_attr = attr;
}

static void set_msg_num(const char *prefix, int value, uint8_t attr)
{
    char buf[80];
    size_t n = 0;
    for (size_t i = 0; prefix[i] != '\0' && n + 12 < sizeof(buf); i++) {
        buf[n++] = prefix[i];
    }
    char digits[12];
    int d = 0;
    unsigned v = (value < 0) ? 0u - (unsigned)value : (unsigned)value;
    do {
        digits[d++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    if (value < 0) {
        buf[n++] = '-';
    }
    while (d > 0) {
        buf[n++] = digits[--d];
    }
    buf[n] = '\0';
    set_msg(buf, attr);
}

static void line_set_empty(int y)
{
    lines[y][0] = '\0';
}

static void clamp_cursor(bool insert)
{
    if (cy < 0) {
        cy = 0;
    }
    if (cy >= nlines) {
        cy = nlines - 1;
    }
    int max = insert ? len_of(cy) : (len_of(cy) > 0 ? len_of(cy) - 1 : 0);
    if (cx > max) {
        cx = max;
    }
    if (cx < 0) {
        cx = 0;
    }
}

static void ensure_visible(void)
{
    if (cy < top) {
        top = cy;
    }
    if (cy >= top + ED_TEXT_ROWS) {
        top = cy - ED_TEXT_ROWS + 1;
    }
    if (cx < left) {
        left = cx;
    }
    if (cx >= left + (int)VGA_COLS) {
        left = cx - (int)VGA_COLS + 1;
    }
}

/* ---------- edição de linhas ---------- */

static bool insert_char(char c)
{
    int n = len_of(cy);
    if (n + 1 >= ED_MAX_COLS) {
        set_msg("linha cheia (limite 119 caracteres)", UI_ATTR_ERROR);
        return false;
    }
    for (int i = n; i >= cx; i--) {
        lines[cy][i + 1] = lines[cy][i];
    }
    lines[cy][cx] = c;
    cx++;
    modified = true;
    return true;
}

static void backspace(void)
{
    if (cx > 0) {
        int n = len_of(cy);
        for (int i = cx - 1; i < n; i++) {
            lines[cy][i] = lines[cy][i + 1];
        }
        cx--;
        modified = true;
    } else if (cy > 0) {
        /* junta com a linha anterior */
        int prev = len_of(cy - 1);
        int n = len_of(cy);
        if (prev + n >= ED_MAX_COLS) {
            set_msg("linha cheia ao juntar", UI_ATTR_ERROR);
            return;
        }
        for (int i = 0; i <= n; i++) {
            lines[cy - 1][prev + i] = lines[cy][i];
        }
        for (int y = cy; y + 1 < nlines; y++) {
            for (int i = 0; i < ED_MAX_COLS; i++) {
                lines[y][i] = lines[y + 1][i];
            }
        }
        nlines--;
        cy--;
        cx = prev;
        modified = true;
    }
}

static void delete_forward(void)
{
    int n = len_of(cy);
    if (cx < n) {
        for (int i = cx; i < n; i++) {
            lines[cy][i] = lines[cy][i + 1];
        }
        modified = true;
    } else if (cy + 1 < nlines) {
        int m = len_of(cy + 1);
        if (n + m >= ED_MAX_COLS) {
            set_msg("linha cheia ao juntar", UI_ATTR_ERROR);
            return;
        }
        for (int i = 0; i <= m; i++) {
            lines[cy][n + i] = lines[cy + 1][i];
        }
        for (int y = cy + 1; y + 1 < nlines; y++) {
            for (int i = 0; i < ED_MAX_COLS; i++) {
                lines[y][i] = lines[y + 1][i];
            }
        }
        nlines--;
        modified = true;
    }
}

static void split_line(void)
{
    if (nlines >= ED_MAX_LINES) {
        set_msg("muitas linhas (limite 512)", UI_ATTR_ERROR);
        return;
    }
    for (int y = nlines; y > cy + 1; y--) {
        for (int i = 0; i < ED_MAX_COLS; i++) {
            lines[y][i] = lines[y - 1][i];
        }
    }
    int n = len_of(cy);
    int k = 0;
    for (int i = cx; i < n; i++) {
        lines[cy + 1][k++] = lines[cy][i];
    }
    lines[cy + 1][k] = '\0';
    lines[cy][cx] = '\0';
    nlines++;
    cy++;
    cx = 0;
    modified = true;
}

/* Insere uma linha vazia em 'at' (empurra as seguintes). */
static bool open_line(int at)
{
    if (nlines >= ED_MAX_LINES) {
        set_msg("muitas linhas (limite 512)", UI_ATTR_ERROR);
        return false;
    }
    for (int y = nlines; y > at; y--) {
        for (int i = 0; i < ED_MAX_COLS; i++) {
            lines[y][i] = lines[y - 1][i];
        }
    }
    nlines++;
    line_set_empty(at);
    modified = true;
    return true;
}

static void delete_line(int at)
{
    for (int i = 0; i < ED_MAX_COLS; i++) {
        yank[i] = lines[at][i];
    }
    has_yank = true;
    if (nlines == 1) {
        line_set_empty(0);
    } else {
        for (int y = at; y + 1 < nlines; y++) {
            for (int i = 0; i < ED_MAX_COLS; i++) {
                lines[y][i] = lines[y + 1][i];
            }
        }
        nlines--;
    }
    if (cy >= nlines) {
        cy = nlines - 1;
    }
    modified = true;
}

static void paste_line(int at)
{
    if (!has_yank) {
        set_msg("nada para colar", UI_ATTR_ERROR);
        return;
    }
    if (!open_line(at)) {
        return;
    }
    for (int i = 0; i < ED_MAX_COLS; i++) {
        lines[at][i] = yank[i];
    }
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t';
}

static void word_forward(void)
{
    int n = len_of(cy);
    int x = cx;
    while (x < n && !is_space(lines[cy][x])) {
        x++;
    }
    while (x < n && is_space(lines[cy][x])) {
        x++;
    }
    if (x >= n && cy + 1 < nlines) {
        cy++;
        cx = 0;
        while (len_of(cy) > 0 && is_space(lines[cy][cx])) {
            cx++;
        }
        return;
    }
    cx = (x < n) ? x : (n > 0 ? n - 1 : 0);
}

static void word_back(void)
{
    if (cx == 0) {
        if (cy > 0) {
            cy--;
            cx = len_of(cy) > 0 ? len_of(cy) - 1 : 0;
        }
        return;
    }
    int x = cx - 1;
    while (x > 0 && is_space(lines[cy][x])) {
        x--;
    }
    while (x > 0 && !is_space(lines[cy][x - 1])) {
        x--;
    }
    cx = x;
}

/* ---------- arquivo ---------- */

/* Texto do buffer (linhas com '\n'). Retorna o tamanho, ou 0 se não couber. */
static size_t buffer_text(char *out, size_t cap)
{
    size_t n = 0;
    for (int y = 0; y < nlines; y++) {
        int m = len_of(y);
        if (n + (size_t)m + 1 > cap) {
            return 0;
        }
        for (int i = 0; i < m; i++) {
            out[n++] = lines[y][i];
        }
        out[n++] = '\n';
    }
    return n;
}

static void load_file(void)
{
    struct vfs_stat st;
    nlines = 1;
    line_set_empty(0);
    if (vfs_stat(path, &st) != VFS_OK) {
        set_msg("[novo arquivo]", UI_ATTR_OK);
        return;
    }
    if (st.is_dir) {
        set_msg("e um diretorio", UI_ATTR_ERROR);
        return;
    }
    char *buf = kmalloc(st.size ? st.size : 1);
    if (buf == NULL) {
        set_msg("memoria esgotada", UI_ATTR_ERROR);
        return;
    }
    size_t got = 0;
    if (vfs_read(path, 0, buf, st.size, &got) != VFS_OK) {
        kfree(buf);
        set_msg("erro ao ler o arquivo", UI_ATTR_ERROR);
        return;
    }

    nlines = 0;
    int col = 0;
    bool open_line_pending = false;   /* há texto da linha atual ainda não fechado */
    bool truncated = false;
    bool too_many = false;
    for (size_t i = 0; i < got && !too_many; i++) {
        char c = buf[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            if (nlines >= ED_MAX_LINES) {
                too_many = true;
                break;
            }
            lines[nlines][col] = '\0';
            nlines++;
            col = 0;
            open_line_pending = false;
            continue;
        }
        if (nlines >= ED_MAX_LINES) {
            too_many = true;
            break;
        }
        open_line_pending = true;
        if (col + 1 < ED_MAX_COLS) {
            lines[nlines][col++] = c;
        } else {
            truncated = true;
        }
    }
    if (!too_many && open_line_pending) {
        if (nlines < ED_MAX_LINES) {
            lines[nlines][col] = '\0';
            nlines++;
        } else {
            too_many = true;
        }
    }
    kfree(buf);
    if (nlines == 0) {
        nlines = 1;
        line_set_empty(0);
    }
    if (too_many) {
        set_msg("arquivo com mais de 512 linhas: resto nao carregado", UI_ATTR_ERROR);
    } else if (truncated) {
        set_msg("linhas longas foram cortadas em 119 caracteres", UI_ATTR_ERROR);
    }
}

static bool save_file(void)
{
    if (path[0] == '\0') {
        set_msg("sem nome: use :w NOME", UI_ATTR_ERROR);
        return false;
    }
    size_t cap = (size_t)ED_MAX_LINES * ED_MAX_COLS;
    char *text = kmalloc(cap);
    if (text == NULL) {
        set_msg("memoria esgotada", UI_ATTR_ERROR);
        return false;
    }
    size_t n = buffer_text(text, cap);
    if (n == 0 && nlines > 0) {
        kfree(text);
        set_msg("arquivo grande demais para salvar", UI_ATTR_ERROR);
        return false;
    }
    int st = vfs_write_file(path, text, n);
    kfree(text);
    if (st != VFS_OK) {
        set_msg(vfs_strerror(st), UI_ATTR_ERROR);
        return false;
    }
    modified = false;
    set_msg_num("gravado: linhas ", nlines, UI_ATTR_OK);
    return true;
}

/* ---------- compilação e execução ---------- */

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

static void run_sink(const char *s, size_t n)
{
    kwrite(s, n);
}

/* Compila o buffer para o .int de mesmo nome (troca .py por .int). */
static void compile_buffer(bool run)
{
    if (path[0] == '\0') {
        set_msg("defina um nome antes: :w NOME.py", UI_ATTR_ERROR);
        return;
    }
    size_t cap = (size_t)ED_MAX_LINES * ED_MAX_COLS;
    char *src = kmalloc(cap);
    uint8_t *img = kmalloc(INT_HEADER_SIZE + INT_MAX_CODE + INT_MAX_CONST);
    if (src == NULL || img == NULL) {
        kfree(src);
        kfree(img);
        set_msg("memoria esgotada", UI_ATTR_ERROR);
        return;
    }
    size_t n = buffer_text(src, cap);
    struct int_error err;
    size_t img_len = 0;
    int rc = int_compile_python(src, n, img, INT_HEADER_SIZE + INT_MAX_CODE + INT_MAX_CONST,
                                &img_len, &err);
    if (rc != 0) {
        if (err.line > 0 && err.line <= nlines) {
            cy = err.line - 1;
            cx = 0;
        }
        char text[80];
        size_t k = 0;
        const char *pre = "erro linha ";
        for (size_t i = 0; pre[i] != '\0'; i++) {
            text[k++] = pre[i];
        }
        int ln = err.line;
        char digits[8];
        int d = 0;
        do {
            digits[d++] = (char)('0' + ln % 10);
            ln /= 10;
        } while (ln > 0);
        while (d > 0) {
            text[k++] = digits[--d];
        }
        text[k++] = ':';
        text[k++] = ' ';
        for (size_t i = 0; err.msg[i] != '\0' && k + 1 < sizeof(text); i++) {
            text[k++] = err.msg[i];
        }
        text[k] = '\0';
        set_msg(text, UI_ATTR_ERROR);
        kfree(img);
        kfree(src);
        return;
    }

    if (!run) {
        char out[VFS_PATH_MAX + 1];
        size_t pl = 0;
        while (path[pl] != '\0') {
            pl++;
        }
        size_t keep = pl;
        if (pl >= 3 && path[pl - 3] == '.' && path[pl - 2] == 'p' && path[pl - 1] == 'y') {
            keep = pl - 3;
        }
        if (keep + 5 > sizeof(out)) {
            set_msg("nome longo demais", UI_ATTR_ERROR);
        } else {
            for (size_t i = 0; i < keep; i++) {
                out[i] = path[i];
            }
            out[keep] = '.';
            out[keep + 1] = 'i';
            out[keep + 2] = 'n';
            out[keep + 3] = 't';
            out[keep + 4] = '\0';
            int st = vfs_write_file(out, img, img_len);
            if (st != VFS_OK) {
                set_msg(vfs_strerror(st), UI_ATTR_ERROR);
            } else {
                set_msg_num("compilado para .int, bytes: ", (int)img_len, UI_ATTR_OK);
            }
        }
    } else {
        vga_clear();
        vga_show_cursor(1);
        kprintf("--- execucao (ESC interrompe) ---\n");
        struct int_error rerr;
        if (int_run(img, img_len, run_sink, stop_on_esc, &rerr) != 0) {
            kprintf("\nerro: %s\n", rerr.msg);
        }
        kprintf("\n--- fim: pressione uma tecla ---");
        ui_wait_key();
        vga_show_cursor(0);
        set_msg("execucao concluida", UI_ATTR_OK);
    }
    kfree(img);
    kfree(src);
}

/* ---------- comandos ':' ---------- */

static bool streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void execute_command(void)
{
    cmdbuf[cmdlen] = '\0';
    char *c = cmdbuf;
    while (*c == ' ') {
        c++;
    }

    if (c[0] == '\0') {
        return;
    }
    if (streq(c, "w")) {
        save_file();
        return;
    }
    if (c[0] == 'w' && c[1] == ' ') {
        char *name = c + 2;
        while (*name == ' ') {
            name++;
        }
        size_t i = 0;
        while (name[i] != '\0' && i < VFS_PATH_MAX) {
            path[i] = name[i];
            i++;
        }
        path[i] = '\0';
        save_file();
        return;
    }
    if (streq(c, "q")) {
        if (modified) {
            set_msg("E37: alteracoes nao gravadas (use :q! ou :wq)", UI_ATTR_ERROR);
        } else {
            quit = true;
        }
        return;
    }
    if (streq(c, "q!")) {
        quit = true;
        return;
    }
    if (streq(c, "wq") || streq(c, "x")) {
        if (save_file()) {
            quit = true;
        }
        return;
    }
    if (streq(c, "int")) {
        compile_buffer(false);
        return;
    }
    if (streq(c, "run")) {
        compile_buffer(true);
        return;
    }
    if (streq(c, "help")) {
        set_msg(":w gravar  :q sair  :wq salvar  :int compilar  :run executar", UI_ATTR_OK);
        return;
    }
    set_msg("comando desconhecido (tente :help)", UI_ATTR_ERROR);
}

/* ---------- teclas por modo ---------- */

static void key_normal(int key)
{
    int prev = pending;
    pending = 0;

    switch (key) {
    case 'h': case KEY_LEFT:
        cx--;
        break;
    case 'l': case KEY_RIGHT:
        cx++;
        break;
    case 'j': case KEY_DOWN:
        cy++;
        break;
    case 'k': case KEY_UP:
        cy--;
        break;
    case '0': case KEY_HOME:
        cx = 0;
        break;
    case '$': case KEY_END:
        cx = len_of(cy);
        break;
    case 'w':
        word_forward();
        break;
    case 'b':
        word_back();
        break;
    case 'G':
        cy = nlines - 1;
        cx = 0;
        break;
    case 'g':
        if (prev == 'g') {
            cy = 0;
            cx = 0;
        } else {
            pending = 'g';
        }
        break;
    case KEY_PGUP:
        cy -= ED_PAGE;
        break;
    case KEY_PGDN:
        cy += ED_PAGE;
        break;
    case 'i':
        mode = ED_INSERT;
        break;
    case 'a':
        if (len_of(cy) > 0) {
            cx++;
        }
        mode = ED_INSERT;
        break;
    case 'I':
        cx = 0;
        mode = ED_INSERT;
        break;
    case 'A':
        cx = len_of(cy);
        mode = ED_INSERT;
        break;
    case 'o':
        if (open_line(cy + 1)) {
            cy++;
            cx = 0;
            mode = ED_INSERT;
        }
        break;
    case 'O':
        if (open_line(cy)) {
            cx = 0;
            mode = ED_INSERT;
        }
        break;
    case 'x':
        if (len_of(cy) > 0) {
            delete_forward();
        }
        break;
    case 'd':
        if (prev == 'd') {
            delete_line(cy);
        } else {
            pending = 'd';
        }
        break;
    case 'y':
        if (prev == 'y') {
            for (int i = 0; i < ED_MAX_COLS; i++) {
                yank[i] = lines[cy][i];
            }
            has_yank = true;
            set_msg("linha copiada", UI_ATTR_OK);
        } else {
            pending = 'y';
        }
        break;
    case 'p':
        paste_line(cy + 1);
        break;
    case 'P':
        paste_line(cy);
        break;
    case ':':
        mode = ED_COMMAND;
        cmdlen = 0;
        cmdbuf[0] = '\0';
        break;
    case KEY_ESC:
        set_msg("", UI_ATTR_NORMAL);
        break;
    default:
        break;
    }
    clamp_cursor(false);
}

static void key_insert(int key)
{
    switch (key) {
    case KEY_ESC:
        mode = ED_NORMAL;
        if (cx > 0) {
            cx--;
        }
        break;
    case '\n':
        split_line();
        break;
    case '\b':
        backspace();
        break;
    case KEY_DEL:
        delete_forward();
        break;
    case '\t':
        for (int i = 0; i < 4; i++) {
            insert_char(' ');
        }
        break;
    case KEY_LEFT:
        if (cx > 0) {
            cx--;
        }
        break;
    case KEY_RIGHT:
        if (cx < len_of(cy)) {
            cx++;
        }
        break;
    case KEY_UP:
        cy--;
        break;
    case KEY_DOWN:
        cy++;
        break;
    case KEY_HOME:
        cx = 0;
        break;
    case KEY_END:
        cx = len_of(cy);
        break;
    case KEY_PGUP:
        cy -= ED_PAGE;
        break;
    case KEY_PGDN:
        cy += ED_PAGE;
        break;
    default:
        if (key >= 32 && key < 127) {
            insert_char((char)key);
        }
        break;
    }
    clamp_cursor(true);
}

static void key_command(int key)
{
    switch (key) {
    case KEY_ESC:
        mode = ED_NORMAL;
        set_msg("", UI_ATTR_NORMAL);
        break;
    case '\n':
        mode = ED_NORMAL;
        execute_command();
        break;
    case '\b':
        if (cmdlen == 0) {
            mode = ED_NORMAL;
        } else {
            cmdlen--;
            cmdbuf[cmdlen] = '\0';
        }
        break;
    default:
        if (key >= 32 && key < 127 && cmdlen + 1 < (int)sizeof(cmdbuf)) {
            cmdbuf[cmdlen++] = (char)key;
            cmdbuf[cmdlen] = '\0';
        }
        break;
    }
}

/* ---------- desenho ---------- */

static void draw(void)
{
    ensure_visible();
    ui_clear(UI_ATTR_NORMAL);

    for (int r = 0; r < ED_TEXT_ROWS; r++) {
        int y = top + r;
        if (y >= nlines) {
            vga_put_at((unsigned)r, 0, '~', UI_ATTR_DIM);
            continue;
        }
        int n = len_of(y);
        for (int c = 0; c < (int)VGA_COLS; c++) {
            int x = left + c;
            if (x >= n) {
                break;
            }
            char ch = lines[y][x];
            if ((unsigned char)ch < 32 || (unsigned char)ch > 126) {
                ch = '.';
            }
            vga_put_at((unsigned)r, (unsigned)c, ch, UI_ATTR_NORMAL);
        }
    }

    /* Barra de status. */
    ui_fill_row(ED_STATUS_ROW, UI_ATTR_INVERSE);
    const char *mname = (mode == ED_INSERT) ? "-- INSERIR --" :
                        (mode == ED_COMMAND) ? "-- COMANDO --" : "-- NORMAL --";
    ui_text(ED_STATUS_ROW, 1, mname, UI_ATTR_INVERSE, 14);
    ui_text(ED_STATUS_ROW, 16, path[0] ? path : "[sem nome]", UI_ATTR_INVERSE, 40);
    if (modified) {
        ui_text(ED_STATUS_ROW, 57, "[+]", UI_ATTR_INVERSE, 4);
    }
    char pos[24] = "L";
    size_t k = 1;
    int lnum = cy + 1;
    char digits[8];
    int d = 0;
    do {
        digits[d++] = (char)('0' + lnum % 10);
        lnum /= 10;
    } while (lnum > 0);
    while (d > 0) {
        pos[k++] = digits[--d];
    }
    pos[k++] = ' ';
    pos[k++] = 'C';
    int cnum = cx + 1;
    char cd[8];
    d = 0;
    do {
        cd[d++] = (char)('0' + cnum % 10);
        cnum /= 10;
    } while (cnum > 0);
    while (d > 0) {
        pos[k++] = cd[--d];
    }
    pos[k] = '\0';
    ui_text(ED_STATUS_ROW, 66, pos, UI_ATTR_INVERSE, 13);

    /* Linha de comando ou de mensagem. */
    ui_fill_row(ED_CMD_ROW, UI_ATTR_NORMAL);
    if (mode == ED_COMMAND) {
        vga_put_at(ED_CMD_ROW, 0, ':', UI_ATTR_NORMAL);
        ui_text(ED_CMD_ROW, 1, cmdbuf, UI_ATTR_NORMAL, 78);
        vga_set_cursor(ED_CMD_ROW, (unsigned)(1 + cmdlen));
    } else {
        ui_text(ED_CMD_ROW, 0, msg, msg_attr, VGA_COLS);
        vga_set_cursor((unsigned)(cy - top), (unsigned)(cx - left));
    }
}

/* ---------- laço principal ---------- */

void editor_run(const char *file)
{
    size_t i = 0;
    while (file[i] != '\0' && i + 1 < sizeof(path)) {
        path[i] = file[i];
        i++;
    }
    path[i] = '\0';

    cy = 0;
    cx = 0;
    top = 0;
    left = 0;
    modified = false;
    mode = ED_NORMAL;
    cmdlen = 0;
    pending = 0;
    quit = false;
    has_yank = false;
    msg[0] = '\0';
    load_file();
    clamp_cursor(false);

    vga_show_cursor(1);
    while (!quit) {
        draw();
        int key = keyboard_getkey();
        switch (mode) {
        case ED_NORMAL:  key_normal(key);  break;
        case ED_INSERT:  key_insert(key);  break;
        case ED_COMMAND: key_command(key); break;
        }
    }
    vga_show_cursor(0);
    vga_clear();
}
