/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdint.h>

#include "lang/int.h"
#include "lang/int_internal.h"

/*
 * Compilador de uma passagem para o subconjunto de Python (ver
 * docs/PYTHON_SUBSET.md). Lexer com indentação, analisador descendente
 * recursivo e geração direta do bytecode .int, com remendo de saltos.
 *
 * Estado é global e estático: o compilador não é reentrante, o que é
 * suficiente para o sistema (uma compilação por vez).
 */

#define MAX_TOKEN   96
#define MAX_STR     256
#define MAX_INDENT  16
#define NAME_LEN    32
#define MAX_LOOP    8
#define MAX_PATCH   32

enum tok_kind {
    T_EOF = 0,
    T_NEWLINE,
    T_INDENT,
    T_DEDENT,
    T_NAME,
    T_NUMBER,
    T_STRING,
    T_OP,
};

struct token {
    int kind;
    int line;
    char text[MAX_TOKEN];   /* nome ou operador */
    int32_t num;            /* T_NUMBER */
    char sval[MAX_STR];     /* T_STRING: conteúdo já decodificado */
    uint32_t slen;
};

struct lexer {
    const char *src;
    size_t len;
    size_t pos;
    int line;
    int indent[MAX_INDENT]; /* pilha de colunas; indent[0] = 0 */
    int top;                /* índice do topo da pilha */
    int pending_dedent;
    int paren;              /* profundidade de parênteses: dentro, quebra de linha é espaço */
    int at_bol;             /* início de linha lógica: medir indentação */
    int eof_nl_done;
    int last;               /* tipo do último token emitido */
};

struct loop_ctx {
    uint32_t breaks[MAX_PATCH];
    int nb;
    uint32_t conts[MAX_PATCH];
    int nc;
};

/* Estado do compilador (global, não reentrante). */
static struct lexer lx;
static struct token cur;
static struct int_error *perr;
static int failed;
static uint8_t code[INT_MAX_CODE];
static uint32_t code_len;
static uint8_t consts[INT_MAX_CONST];
static uint32_t const_len;
static char names[INT_MAX_SLOTS][NAME_LEN];
static int nslots;
static int hidden_count;
static int loop_depth;
static struct loop_ctx loops[MAX_LOOP];

/* ---------- utilidades ---------- */

static int str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void copy_bounded(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static const char *const reserved[] = {
    "and", "break", "continue", "elif", "else", "False", "for", "if", "in",
    "not", "or", "pass", "True", "while", NULL,
};

/* Palavras de Python que o subconjunto não implementa (erro claro, não nome de variável). */
static const char *const unsupported[] = {
    "def", "class", "import", "from", "return", "with", "try", "except",
    "finally", "raise", "lambda", "global", "nonlocal", "del", "is", "None",
    "assert", "yield", "async", "await", "as", NULL,
};

static int in_list(const char *word, const char *const *list)
{
    for (int i = 0; list[i] != NULL; i++) {
        if (str_eq(word, list[i])) {
            return 1;
        }
    }
    return 0;
}

static int is_reserved(const char *word)
{
    return in_list(word, reserved);
}

static int is_unsupported(const char *word)
{
    return in_list(word, unsupported);
}

/* ---------- erros ---------- */

static void fail_at(int line, const char *msg)
{
    if (!failed) {
        failed = 1;
        int_set_error(perr, line, msg);
    }
}

/* Erro "<o que faltou> perto de 'TOKEN'", com o token atual. */
static void fail_near(const char *what)
{
    char buf[sizeof(perr->msg)];
    size_t n = 0;
    for (size_t i = 0; what[i] != '\0' && n + 1 < sizeof(buf); i++) {
        buf[n++] = what[i];
    }
    const char *tail;
    char tok[MAX_TOKEN];
    switch (cur.kind) {
    case T_NEWLINE: tail = " (fim de linha)"; break;
    case T_EOF:     tail = " (fim do arquivo)"; break;
    case T_INDENT:
    case T_DEDENT:  tail = " (indentacao)"; break;
    case T_STRING:  tail = " (string)"; break;
    case T_NUMBER:  tail = " (numero)"; break;
    default:
        tok[0] = '\'';
        copy_bounded(tok + 1, cur.text, sizeof(tok) - 2);
        {
            size_t l = 0;
            while (tok[l] != '\0') {
                l++;
            }
            tok[l] = '\'';
            tok[l + 1] = '\0';
        }
        tail = tok;
        break;
    }
    const char *mid = " perto de ";
    for (size_t i = 0; mid[i] != '\0' && n + 1 < sizeof(buf); i++) {
        buf[n++] = mid[i];
    }
    for (size_t i = 0; tail[i] != '\0' && n + 1 < sizeof(buf); i++) {
        buf[n++] = tail[i];
    }
    buf[n] = '\0';
    fail_at(cur.line, buf);
}

/* ---------- analisador léxico ---------- */

static void lex_raw(struct lexer *l, struct token *t);

static void lex(struct lexer *l, struct token *t)
{
    lex_raw(l, t);
    l->last = t->kind;
}

static int is_name_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static void set_op(struct token *t, const char *op)
{
    t->kind = T_OP;
    copy_bounded(t->text, op, sizeof(t->text));
}

static void lex_raw(struct lexer *l, struct token *t)
{
    t->text[0] = '\0';
    t->sval[0] = '\0';
    t->slen = 0;
    t->num = 0;
    t->line = l->line;

    for (;;) {
        if (failed) {
            t->kind = T_EOF;
            return;
        }
        if (l->pending_dedent > 0) {
            l->pending_dedent--;
            t->kind = T_DEDENT;
            return;
        }

        if (l->at_bol && l->paren == 0) {
            size_t p = l->pos;
            int col = 0;
            while (p < l->len && l->src[p] == ' ') {
                col++;
                p++;
            }
            if (p < l->len && l->src[p] == '\t') {
                fail_at(l->line, "tabulacao nao suportada: use espacos na indentacao");
                t->kind = T_EOF;
                return;
            }
            if (p < l->len && (l->src[p] == '\n' || l->src[p] == '#' || l->src[p] == '\r')) {
                /* linha vazia ou só comentário: não conta para a indentação */
                while (p < l->len && l->src[p] != '\n') {
                    p++;
                }
                if (p < l->len) {
                    p++;
                    l->line++;
                }
                l->pos = p;
                continue;
            }
            l->pos = p;
            l->at_bol = 0;
            if (p < l->len) {
                if (col > l->indent[l->top]) {
                    if (l->top + 1 >= MAX_INDENT) {
                        fail_at(l->line, "indentacao profunda demais");
                        t->kind = T_EOF;
                        return;
                    }
                    l->top++;
                    l->indent[l->top] = col;
                    t->kind = T_INDENT;
                    return;
                }
                while (col < l->indent[l->top]) {
                    l->top--;
                    l->pending_dedent++;
                }
                if (col != l->indent[l->top]) {
                    fail_at(l->line, "indentacao inconsistente");
                    t->kind = T_EOF;
                    return;
                }
                if (l->pending_dedent > 0) {
                    l->pending_dedent--;
                    t->kind = T_DEDENT;
                    return;
                }
            }
        }

        while (l->pos < l->len && (l->src[l->pos] == ' ' || l->src[l->pos] == '\r')) {
            l->pos++;
        }

        if (l->pos >= l->len) {
            if (!l->eof_nl_done) {
                l->eof_nl_done = 1;
                if (l->last != T_NEWLINE && l->last != T_DEDENT && l->last != T_INDENT) {
                    t->kind = T_NEWLINE;
                    return;
                }
            }
            if (l->top > 0) {
                l->top--;
                t->kind = T_DEDENT;
                return;
            }
            t->kind = T_EOF;
            return;
        }

        char c = l->src[l->pos];
        if (c == '#') {
            while (l->pos < l->len && l->src[l->pos] != '\n') {
                l->pos++;
            }
            continue;
        }
        if (c == '\n') {
            t->line = l->line;
            l->pos++;
            l->line++;
            if (l->paren > 0) {
                continue;
            }
            l->at_bol = 1;
            t->kind = T_NEWLINE;
            return;
        }

        if (is_name_start(c)) {
            size_t n = 0;
            while (l->pos < l->len && (is_name_start(l->src[l->pos]) || is_digit(l->src[l->pos]))) {
                if (n + 1 >= MAX_TOKEN) {
                    fail_at(l->line, "nome muito longo");
                    t->kind = T_EOF;
                    return;
                }
                t->text[n++] = l->src[l->pos++];
            }
            t->text[n] = '\0';
            t->kind = T_NAME;
            return;
        }

        if (is_digit(c)) {
            uint32_t acc = 0;
            while (l->pos < l->len && is_digit(l->src[l->pos])) {
                uint32_t d = (uint32_t)(l->src[l->pos] - '0');
                if (acc > (2147483647u - d) / 10u) {
                    fail_at(l->line, "numero grande demais (limite 2147483647)");
                    t->kind = T_EOF;
                    return;
                }
                acc = acc * 10u + d;
                l->pos++;
            }
            if (l->pos < l->len && is_name_start(l->src[l->pos])) {
                fail_at(l->line, "numero mal formado");
                t->kind = T_EOF;
                return;
            }
            t->kind = T_NUMBER;
            t->num = (int32_t)acc;
            return;
        }

        if (c == '"' || c == '\'') {
            if (l->pos + 2 < l->len && l->src[l->pos + 1] == c && l->src[l->pos + 2] == c) {
                fail_at(l->line, "aspas triplas nao suportadas");
                t->kind = T_EOF;
                return;
            }
            l->pos++;
            uint32_t n = 0;
            for (;;) {
                if (l->pos >= l->len || l->src[l->pos] == '\n') {
                    fail_at(l->line, "string sem fechamento");
                    t->kind = T_EOF;
                    return;
                }
                char ch = l->src[l->pos++];
                if (ch == c) {
                    break;
                }
                if (ch == '\\') {
                    if (l->pos >= l->len) {
                        fail_at(l->line, "string sem fechamento");
                        t->kind = T_EOF;
                        return;
                    }
                    char e = l->src[l->pos++];
                    switch (e) {
                    case 'n':  ch = '\n'; break;
                    case 't':  ch = '\t'; break;
                    case 'r':  ch = '\r'; break;
                    case '0':  ch = '\0'; break;
                    case '\\': ch = '\\'; break;
                    case '\'': ch = '\''; break;
                    case '"':  ch = '"';  break;
                    default:
                        fail_at(l->line, "escape invalido em string (use \\n, \\t, \\\\, \\' ou \\\")");
                        t->kind = T_EOF;
                        return;
                    }
                }
                if (n + 1 >= MAX_STR) {
                    fail_at(l->line, "string longa demais (limite 255 bytes)");
                    t->kind = T_EOF;
                    return;
                }
                t->sval[n++] = ch;
            }
            t->sval[n] = '\0';
            t->slen = n;
            t->kind = T_STRING;
            return;
        }

        /* Operadores: primeiro os de dois caracteres. */
        if (l->pos + 1 < l->len) {
            char d = l->src[l->pos + 1];
            const char *two = NULL;
            if (c == '=' && d == '=') two = "==";
            else if (c == '!' && d == '=') two = "!=";
            else if (c == '<' && d == '=') two = "<=";
            else if (c == '>' && d == '=') two = ">=";
            else if (c == '/' && d == '/') two = "//";
            else if (c == '+' && d == '=') two = "+=";
            else if (c == '-' && d == '=') two = "-=";
            else if (c == '*' && d == '=') two = "*=";
            if (two != NULL) {
                l->pos += 2;
                set_op(t, two);
                return;
            }
        }
        char one[2] = { c, '\0' };
        switch (c) {
        case '+': case '-': case '*': case '%': case '<': case '>': case '=':
        case ',': case ':': case '/':
            l->pos++;
            set_op(t, one);
            return;
        case '(':
            l->paren++;
            l->pos++;
            set_op(t, one);
            return;
        case ')':
            if (l->paren > 0) {
                l->paren--;
            }
            l->pos++;
            set_op(t, one);
            return;
        default:
            fail_at(l->line, "caractere nao suportado no codigo");
            t->kind = T_EOF;
            return;
        }
    }
}

/* ---------- emissão de código ---------- */

static void put8(uint8_t b)
{
    if (code_len >= INT_MAX_CODE) {
        fail_at(cur.line, "programa grande demais (limite 32 KiB de codigo)");
        return;
    }
    code[code_len++] = b;
}

static void put32(uint32_t v)
{
    put8((uint8_t)(v & 0xFF));
    put8((uint8_t)((v >> 8) & 0xFF));
    put8((uint8_t)((v >> 16) & 0xFF));
    put8((uint8_t)((v >> 24) & 0xFF));
}

static void emit_simple(uint8_t op)
{
    put8(op);
}

/* Emite um salto com alvo ainda desconhecido; devolve a posição do operando. */
static uint32_t emit_jump(uint8_t op)
{
    put8(op);
    uint32_t pos = code_len;
    put32(0);
    return pos;
}

static void emit_jump_to(uint8_t op, uint32_t target)
{
    put8(op);
    put32(target);
}

static void patch32(uint32_t pos, uint32_t v)
{
    if (pos + 4 > code_len) {
        fail_at(cur.line, "erro interno: remendo fora do codigo");
        return;
    }
    code[pos] = (uint8_t)(v & 0xFF);
    code[pos + 1] = (uint8_t)((v >> 8) & 0xFF);
    code[pos + 2] = (uint8_t)((v >> 16) & 0xFF);
    code[pos + 3] = (uint8_t)((v >> 24) & 0xFF);
}

static void patch_here(uint32_t pos)
{
    patch32(pos, code_len);
}

static void emit_push_int(int32_t v)
{
    put8(OP_PUSH_INT);
    put32((uint32_t)v);
}

static void emit_push_str(const char *s, uint32_t n)
{
    if (const_len + n > INT_MAX_CONST) {
        fail_at(cur.line, "texto constante grande demais (limite 4 KiB no programa)");
        return;
    }
    uint32_t off = const_len;
    for (uint32_t i = 0; i < n; i++) {
        consts[const_len++] = (uint8_t)s[i];
    }
    put8(OP_PUSH_STR);
    put32(off);
    put8((uint8_t)(n & 0xFF));
    put8((uint8_t)((n >> 8) & 0xFF));
}

static void emit_load(int slot)
{
    put8(OP_LOAD);
    put8((uint8_t)slot);
}

static void emit_store(int slot)
{
    put8(OP_STORE);
    put8((uint8_t)slot);
}

/* ---------- variáveis ---------- */

static int find_slot(const char *name)
{
    for (int i = 0; i < nslots; i++) {
        if (str_eq(names[i], name)) {
            return i;
        }
    }
    return -1;
}

/* Devolve o slot de 'name', criando-o se 'create'. -1 em erro (já registrado). */
static int slot_for(const char *name, int create)
{
    int s = find_slot(name);
    if (s >= 0 || !create) {
        return s;
    }
    if (nslots >= (int)INT_MAX_SLOTS) {
        fail_at(cur.line, "muitas variaveis no programa (limite 64)");
        return -1;
    }
    copy_bounded(names[nslots], name, NAME_LEN);
    return nslots++;
}

static int new_hidden_slot(void)
{
    char name[NAME_LEN];
    name[0] = '#';
    name[1] = (char)('a' + (hidden_count % 26));
    name[2] = (char)('0' + ((hidden_count / 26) % 10));
    name[3] = '\0';
    hidden_count++;
    if (nslots >= (int)INT_MAX_SLOTS) {
        fail_at(cur.line, "muitas variaveis no programa (limite 64)");
        return -1;
    }
    copy_bounded(names[nslots], name, NAME_LEN);
    return nslots++;
}

/* ---------- analisador sintático ---------- */

static void advance(void)
{
    lex(&lx, &cur);
}

static int is_op(const char *s)
{
    return cur.kind == T_OP && str_eq(cur.text, s);
}

static int is_kw(const char *s)
{
    return cur.kind == T_NAME && str_eq(cur.text, s);
}

static void expect_op(const char *s)
{
    if (failed) {
        return;
    }
    if (is_op(s)) {
        advance();
        return;
    }
    char what[32];
    size_t n = 0;
    const char *pre = "esperado '";
    for (size_t i = 0; pre[i] != '\0' && n + 4 < sizeof(what); i++) {
        what[n++] = pre[i];
    }
    for (size_t i = 0; s[i] != '\0' && n + 4 < sizeof(what); i++) {
        what[n++] = s[i];
    }
    what[n++] = '\'';
    what[n] = '\0';
    fail_near(what);
}

/* Espia o próximo token sem consumir (para distinguir atribuição). */
static int peek_is_assign(void)
{
    struct lexer save = lx;
    struct token t;
    lex(&lx, &t);
    int r = (t.kind == T_OP) &&
            (str_eq(t.text, "=") || str_eq(t.text, "+=") || str_eq(t.text, "-="));
    lx = save;
    return r;
}

static void expression(void);
static void simple_stmt(void);

/* Converte o operador de comparação atual em opcode, ou -1. */
static int cmp_opcode(void)
{
    if (cur.kind != T_OP) {
        return -1;
    }
    if (str_eq(cur.text, "==")) return OP_EQ;
    if (str_eq(cur.text, "!=")) return OP_NE;
    if (str_eq(cur.text, "<"))  return OP_LT;
    if (str_eq(cur.text, "<=")) return OP_LE;
    if (str_eq(cur.text, ">"))  return OP_GT;
    if (str_eq(cur.text, ">=")) return OP_GE;
    return -1;
}

static int builtin_id(const char *name)
{
    if (str_eq(name, "print")) return BI_PRINT;
    if (str_eq(name, "len"))   return BI_LEN;
    if (str_eq(name, "str"))   return BI_STR;
    if (str_eq(name, "int"))   return BI_INT;
    return -1;
}

static void primary(void)
{
    if (failed) {
        return;
    }
    if (cur.kind == T_NUMBER) {
        emit_push_int(cur.num);
        advance();
        return;
    }
    if (cur.kind == T_STRING) {
        emit_push_str(cur.sval, cur.slen);
        advance();
        return;
    }
    if (is_kw("True")) {
        emit_push_int(0);          /* not 0 == True */
        emit_simple(OP_NOT);
        advance();
        return;
    }
    if (is_kw("False")) {
        emit_push_int(0);          /* not not 0 == False */
        emit_simple(OP_NOT);
        emit_simple(OP_NOT);
        advance();
        return;
    }
    if (is_op("(")) {
        advance();
        expression();
        expect_op(")");
        return;
    }
    if (cur.kind == T_NAME) {
        if (is_unsupported(cur.text)) {
            char msg[96] = "recurso nao suportado: ";
            size_t n = 0;
            while (msg[n] != '\0') {
                n++;
            }
            copy_bounded(msg + n, cur.text, sizeof(msg) - n - 1);
            fail_at(cur.line, msg);
            return;
        }
        if (is_reserved(cur.text)) {
            fail_near("expressao");
            return;
        }

        char name[MAX_TOKEN];
        copy_bounded(name, cur.text, sizeof(name));
        advance();

        if (is_op("(")) {
            int id = builtin_id(name);
            if (id < 0) {
                char msg[96] = "funcao nao suportada: ";
                size_t n = 0;
                while (msg[n] != '\0') {
                    n++;
                }
                copy_bounded(msg + n, name, sizeof(msg) - n - 1);
                fail_at(cur.line, msg);
                return;
            }
            advance();
            int argc = 0;
            if (!is_op(")")) {
                expression();
                argc++;
                while (!failed && is_op(",")) {
                    advance();
                    expression();
                    argc++;
                }
            }
            expect_op(")");
            if (failed) {
                return;
            }
            if (id != BI_PRINT && argc != 1) {
                fail_at(cur.line, "esta funcao recebe exatamente um argumento");
                return;
            }
            put8(OP_CALL);
            put8((uint8_t)id);
            put8((uint8_t)argc);
            return;
        }

        int slot = slot_for(name, 0);
        if (slot < 0) {
            char msg[96] = "nome nao definido antes do uso: ";
            size_t n = 0;
            while (msg[n] != '\0') {
                n++;
            }
            copy_bounded(msg + n, name, sizeof(msg) - n - 1);
            fail_at(cur.line, msg);
            return;
        }
        emit_load(slot);
        return;
    }
    fail_near("expressao");
}

static void factor(void)
{
    if (failed) {
        return;
    }
    if (is_op("-")) {
        advance();
        factor();
        emit_simple(OP_NEG);
        return;
    }
    if (is_op("+")) {
        advance();
        factor();
        return;
    }
    primary();
}

static void term(void)
{
    factor();
    while (!failed && (is_op("*") || is_op("//") || is_op("%"))) {
        uint8_t op = is_op("*") ? OP_MUL : (is_op("//") ? OP_FLOORDIV : OP_MOD);
        advance();
        factor();
        emit_simple(op);
    }
}

static void arith(void)
{
    term();
    while (!failed && (is_op("+") || is_op("-"))) {
        uint8_t op = is_op("+") ? OP_ADD : OP_SUB;
        advance();
        term();
        emit_simple(op);
    }
}

static void comparison(void)
{
    arith();
    if (failed) {
        return;
    }
    int op = cmp_opcode();
    if (op < 0) {
        return;
    }
    advance();
    arith();
    if (cmp_opcode() >= 0) {
        fail_at(cur.line, "comparacao encadeada (a < b < c) nao suportada: use and");
        return;
    }
    emit_simple((uint8_t)op);
}

static void not_test(void)
{
    if (failed) {
        return;
    }
    if (is_kw("not")) {
        advance();
        not_test();
        emit_simple(OP_NOT);
        return;
    }
    comparison();
}

static void and_test(void)
{
    uint32_t jumps[MAX_PATCH];
    int nj = 0;
    not_test();
    while (!failed && is_kw("and")) {
        advance();
        if (nj >= MAX_PATCH) {
            fail_at(cur.line, "expressao 'and' longa demais");
            return;
        }
        jumps[nj++] = emit_jump(OP_JF_KEEP);
        not_test();
    }
    for (int i = 0; i < nj; i++) {
        patch_here(jumps[i]);
    }
}

static void or_test(void)
{
    uint32_t jumps[MAX_PATCH];
    int nj = 0;
    and_test();
    while (!failed && is_kw("or")) {
        advance();
        if (nj >= MAX_PATCH) {
            fail_at(cur.line, "expressao 'or' longa demais");
            return;
        }
        jumps[nj++] = emit_jump(OP_JT_KEEP);
        and_test();
    }
    for (int i = 0; i < nj; i++) {
        patch_here(jumps[i]);
    }
}

static void expression(void)
{
    or_test();
}

/* ---------- blocos e comandos ---------- */

static void statement(void);

static void block(void)
{
    if (failed) {
        return;
    }
    if (cur.kind == T_NEWLINE) {
        advance();
        if (cur.kind != T_INDENT) {
            fail_at(cur.line, "esperado bloco indentado depois de ':'");
            return;
        }
        advance();
        while (!failed && cur.kind != T_DEDENT && cur.kind != T_EOF) {
            statement();
        }
        if (failed) {
            return;
        }
        if (cur.kind != T_DEDENT) {
            fail_at(cur.line, "bloco mal fechado");
            return;
        }
        advance();
        return;
    }
    /* bloco de uma linha: "if x: print(x)" */
    simple_stmt();
    if (failed) {
        return;
    }
    if (cur.kind == T_NEWLINE) {
        advance();
    } else if (cur.kind != T_EOF) {
        fail_near("fim de linha");
    }
}

static void push_loop(void)
{
    if (loop_depth >= MAX_LOOP) {
        fail_at(cur.line, "laços aninhados demais (limite 8)");
        return;
    }
    loops[loop_depth].nb = 0;
    loops[loop_depth].nc = 0;
    loop_depth++;
}

static void add_patch(uint32_t *list, int *count, uint32_t value)
{
    if (*count >= MAX_PATCH) {
        fail_at(cur.line, "laço com saltos demais (limite 32 break/continue)");
        return;
    }
    list[(*count)++] = value;
}

static void simple_stmt(void)
{
    if (failed) {
        return;
    }
    if (is_kw("pass")) {
        advance();
        return;
    }
    if (is_kw("break")) {
        if (loop_depth == 0) {
            fail_at(cur.line, "'break' fora de laco");
            return;
        }
        advance();
        add_patch(loops[loop_depth - 1].breaks, &loops[loop_depth - 1].nb,
                  emit_jump(OP_JMP));
        return;
    }
    if (is_kw("continue")) {
        if (loop_depth == 0) {
            fail_at(cur.line, "'continue' fora de laco");
            return;
        }
        advance();
        add_patch(loops[loop_depth - 1].conts, &loops[loop_depth - 1].nc,
                  emit_jump(OP_JMP));
        return;
    }
    if (cur.kind == T_NAME && is_unsupported(cur.text)) {
        fail_at(cur.line, "recurso nao suportado nesta versao (veja docs/PYTHON_SUBSET.md)");
        return;
    }
    if (cur.kind == T_NAME && !is_reserved(cur.text) && peek_is_assign()) {
        char name[MAX_TOKEN];
        copy_bounded(name, cur.text, sizeof(name));
        advance();
        char op[4];
        copy_bounded(op, cur.text, sizeof(op));
        advance();

        int slot = slot_for(name, 1);
        if (slot < 0) {
            return;
        }
        if (str_eq(op, "=")) {
            expression();
            if (failed) {
                return;
            }
            emit_store(slot);
            return;
        }
        emit_load(slot);
        expression();
        if (failed) {
            return;
        }
        emit_simple(str_eq(op, "+=") ? OP_ADD : OP_SUB);
        emit_store(slot);
        return;
    }
    expression();
    if (failed) {
        return;
    }
    emit_simple(OP_POP);
}

static void while_stmt(void)
{
    advance();
    uint32_t start = code_len;
    expression();
    expect_op(":");
    if (failed) {
        return;
    }
    uint32_t jf = emit_jump(OP_JF);
    push_loop();
    block();
    if (failed) {
        return;
    }
    emit_jump_to(OP_JMP, start);
    patch_here(jf);

    struct loop_ctx lc = loops[loop_depth - 1];
    loop_depth--;
    for (int i = 0; i < lc.nc; i++) {
        patch32(lc.conts[i], start);
    }
    for (int i = 0; i < lc.nb; i++) {
        patch_here(lc.breaks[i]);
    }
}

static void for_stmt(void)
{
    advance();
    if (cur.kind != T_NAME || is_reserved(cur.text) || is_unsupported(cur.text)) {
        fail_near("nome de variavel depois de 'for'");
        return;
    }
    char var[MAX_TOKEN];
    copy_bounded(var, cur.text, sizeof(var));
    advance();
    if (!is_kw("in")) {
        fail_near("esperado 'in'");
        return;
    }
    advance();
    if (!is_kw("range")) {
        fail_at(cur.line, "so e suportado: for NOME in range(...)");
        return;
    }
    advance();
    expect_op("(");
    if (failed) {
        return;
    }

    int var_slot = slot_for(var, 1);
    int ctr = new_hidden_slot();
    int end = new_hidden_slot();
    if (failed || var_slot < 0 || ctr < 0 || end < 0) {
        return;
    }

    expression();
    if (failed) {
        return;
    }
    if (is_op(",")) {
        advance();
        emit_store(ctr);               /* primeiro argumento: início */
        expression();
        if (failed) {
            return;
        }
        emit_store(end);               /* segundo argumento: fim (exclusivo) */
    } else {
        emit_store(end);               /* único argumento: fim; início = 0 */
        emit_push_int(0);
        emit_store(ctr);
    }
    expect_op(")");
    expect_op(":");
    if (failed) {
        return;
    }

    uint32_t top = code_len;
    emit_load(ctr);
    emit_load(end);
    emit_simple(OP_LT);
    uint32_t jf = emit_jump(OP_JF);
    emit_load(ctr);
    emit_store(var_slot);

    push_loop();
    block();
    if (failed) {
        return;
    }

    uint32_t incr = code_len;
    emit_load(ctr);
    emit_push_int(1);
    emit_simple(OP_ADD);
    emit_store(ctr);
    emit_jump_to(OP_JMP, top);
    patch_here(jf);

    struct loop_ctx lc = loops[loop_depth - 1];
    loop_depth--;
    for (int i = 0; i < lc.nc; i++) {
        patch32(lc.conts[i], incr);
    }
    for (int i = 0; i < lc.nb; i++) {
        patch_here(lc.breaks[i]);
    }
}

static void if_stmt(void)
{
    uint32_t ends[MAX_PATCH];
    int ne = 0;
    int has_else = 0;

    advance();
    expression();
    expect_op(":");
    if (failed) {
        return;
    }
    uint32_t jf = emit_jump(OP_JF);
    block();

    while (!failed && is_kw("elif")) {
        if (ne >= MAX_PATCH) {
            fail_at(cur.line, "if com elif demais");
            return;
        }
        ends[ne++] = emit_jump(OP_JMP);
        patch_here(jf);
        advance();
        expression();
        expect_op(":");
        if (failed) {
            return;
        }
        jf = emit_jump(OP_JF);
        block();
    }
    if (failed) {
        return;
    }
    if (is_kw("else")) {
        advance();
        if (ne >= MAX_PATCH) {
            fail_at(cur.line, "if com elif demais");
            return;
        }
        ends[ne++] = emit_jump(OP_JMP);
        patch_here(jf);
        expect_op(":");
        if (failed) {
            return;
        }
        block();
        has_else = 1;
    }
    if (failed) {
        return;
    }
    if (!has_else) {
        patch_here(jf);
    }
    for (int i = 0; i < ne; i++) {
        patch_here(ends[i]);
    }
}

static void statement(void)
{
    if (failed) {
        return;
    }
    if (is_kw("if")) {
        if_stmt();
        return;
    }
    if (is_kw("while")) {
        while_stmt();
        return;
    }
    if (is_kw("for")) {
        for_stmt();
        return;
    }
    simple_stmt();
    if (failed) {
        return;
    }
    if (cur.kind == T_NEWLINE) {
        advance();
    } else if (cur.kind != T_EOF) {
        fail_near("fim de linha");
    }
}

/* ---------- montagem da imagem ---------- */

static void write_header(uint8_t *out)
{
    out[0] = INT_MAGIC_0;
    out[1] = INT_MAGIC_1;
    out[2] = INT_MAGIC_2;
    out[3] = INT_MAGIC_3;
    out[4] = (uint8_t)(INT_VERSION & 0xFF);
    out[5] = (uint8_t)(INT_VERSION >> 8);
    out[6] = (uint8_t)(INT_HEADER_SIZE & 0xFF);
    out[7] = (uint8_t)(INT_HEADER_SIZE >> 8);
    uint32_t fields[4] = { code_len, const_len, (uint32_t)nslots, 0 };
    for (int f = 0; f < 4; f++) {
        for (int b = 0; b < 4; b++) {
            out[8 + f * 4 + b] = (uint8_t)((fields[f] >> (8 * b)) & 0xFF);
        }
    }
    for (int i = 24; i < (int)INT_HEADER_SIZE; i++) {
        out[i] = 0;
    }
}

static void reset_state(const char *src, size_t len, struct int_error *err)
{
    lx.src = src;
    lx.len = len;
    lx.pos = 0;
    lx.line = 1;
    lx.indent[0] = 0;
    lx.top = 0;
    lx.pending_dedent = 0;
    lx.paren = 0;
    lx.at_bol = 1;
    lx.eof_nl_done = 0;
    lx.last = T_NEWLINE;

    perr = err;
    failed = 0;
    code_len = 0;
    const_len = 0;
    nslots = 0;
    hidden_count = 0;
    loop_depth = 0;
    for (int i = 0; i < (int)INT_MAX_SLOTS; i++) {
        names[i][0] = '\0';
    }
}

int int_compile_python(const char *src, size_t len, uint8_t *out, size_t cap,
                       size_t *out_len, struct int_error *err)
{
    reset_state(src, len, err);

    advance();
    while (!failed && cur.kind != T_EOF) {
        if (cur.kind == T_NEWLINE) {
            advance();
            continue;
        }
        if (cur.kind == T_INDENT) {
            fail_at(cur.line, "indentacao inesperada");
            break;
        }
        statement();
    }
    if (failed) {
        return -1;
    }

    emit_simple(OP_HALT);
    if (failed) {
        return -1;
    }

    size_t total = (size_t)INT_HEADER_SIZE + code_len + const_len;
    if (total > cap) {
        int_set_error(err, 0, "buffer de saida pequeno para a imagem .int");
        return -1;
    }

    write_header(out);
    for (uint32_t i = 0; i < code_len; i++) {
        out[INT_HEADER_SIZE + i] = code[i];
    }
    for (uint32_t i = 0; i < const_len; i++) {
        out[INT_HEADER_SIZE + code_len + i] = consts[i];
    }
    uint32_t sum = int_fnv1a(out + INT_HEADER_SIZE, code_len + const_len);
    for (int b = 0; b < 4; b++) {
        out[20 + b] = (uint8_t)((sum >> (8 * b)) & 0xFF);
    }

    /* Rede de segurança: a imagem gerada precisa passar pelo verificador. */
    struct int_info info;
    if (int_verify(out, total, &info, err) != 0) {
        int_set_error(err, 0, "erro interno: imagem gerada invalida");
        return -1;
    }

    *out_len = total;
    return 0;
}
