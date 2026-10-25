/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdint.h>

#include "lang/int.h"
#include "lang/int_internal.h"

/*
 * Máquina virtual de pilha para .int. Só executa imagens que passaram por
 * int_verify, e ainda assim confere cada pilha, tipo e divisão em tempo de
 * execução. Não usa alocação dinâmica: strings criadas em execução ficam numa
 * arena fixa, descartada ao fim do programa.
 */

#define STACK_MAX    128
#define ARENA_BYTES  4096
#define STOP_EVERY   1024u

enum value_kind {
    V_UNBOUND = 0,      /* variável ainda sem valor (só em slots) */
    V_NONE,
    V_BOOL,
    V_INT,
    V_STR,
};

struct value {
    uint8_t kind;
    int32_t i;          /* V_BOOL e V_INT */
    const char *s;      /* V_STR: aponta para a imagem ou para a arena */
    uint32_t len;
};

static struct value stack[STACK_MAX];
static struct value slots[INT_MAX_SLOTS];
static char arena[ARENA_BYTES];
static size_t arena_used;

static int is_int_like(const struct value *v)
{
    return v->kind == V_INT || v->kind == V_BOOL;
}

static int truthy(const struct value *v)
{
    switch (v->kind) {
    case V_BOOL:
    case V_INT:  return v->i != 0;
    case V_STR:  return v->len != 0;
    default:     return 0;              /* None */
    }
}

static struct value make_int(int32_t i)
{
    struct value v = { V_INT, i, NULL, 0 };
    return v;
}

static struct value make_bool(int b)
{
    struct value v = { V_BOOL, b ? 1 : 0, NULL, 0 };
    return v;
}

static struct value make_str(const char *s, uint32_t len)
{
    struct value v = { V_STR, 0, s, len };
    return v;
}

static struct value make_none(void)
{
    struct value v = { V_NONE, 0, NULL, 0 };
    return v;
}

/* Escreve a representação de 'i' em 'buf' (capacidade 12). Retorna o tamanho. */
static size_t format_int(char *buf, int32_t i)
{
    char tmp[12];
    size_t n = 0;
    uint32_t m = (i < 0) ? 0u - (uint32_t)i : (uint32_t)i;
    do {
        tmp[n++] = (char)('0' + (m % 10u));
        m /= 10u;
    } while (m != 0);

    size_t k = 0;
    if (i < 0) {
        buf[k++] = '-';
    }
    while (n > 0) {
        buf[k++] = tmp[--n];
    }
    return k;
}

static char *arena_alloc(size_t n)
{
    if (n > ARENA_BYTES - arena_used) {
        return NULL;
    }
    char *p = arena + arena_used;
    arena_used += n;
    return p;
}

static void emit(int_out_fn out, const char *s, size_t n)
{
    if (out != NULL && n != 0) {
        out(s, n);
    }
}

/* Escreve o valor como print o mostraria. */
static void emit_value(int_out_fn out, const struct value *v)
{
    char buf[12];
    switch (v->kind) {
    case V_NONE:
        emit(out, "None", 4);
        break;
    case V_BOOL:
        if (v->i) {
            emit(out, "True", 4);
        } else {
            emit(out, "False", 5);
        }
        break;
    case V_INT:
        emit(out, buf, format_int(buf, v->i));
        break;
    case V_STR:
        emit(out, v->s, v->len);
        break;
    default:
        break;
    }
}

/* Converte texto decimal (com sinal opcional) em inteiro de 32 bits. */
static int parse_int(const char *s, uint32_t len, int32_t *out)
{
    uint32_t i = 0;
    int neg = 0;
    if (len > 0 && (s[0] == '-' || s[0] == '+')) {
        neg = (s[0] == '-');
        i = 1;
    }
    if (i >= len) {
        return -1;
    }
    uint32_t acc = 0;
    for (; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        uint32_t d = (uint32_t)(s[i] - '0');
        if (acc > (2147483648u - d) / 10u) {
            return -2;                  /* fora do intervalo de 32 bits */
        }
        acc = acc * 10u + d;
    }
    if (!neg && acc > 2147483647u) {
        return -2;
    }
    *out = neg ? (int32_t)(0u - acc) : (int32_t)acc;
    return 0;
}

static int str_compare(const struct value *a, const struct value *b)
{
    uint32_t n = (a->len < b->len) ? a->len : b->len;
    for (uint32_t i = 0; i < n; i++) {
        if (a->s[i] != b->s[i]) {
            return (unsigned char)a->s[i] < (unsigned char)b->s[i] ? -1 : 1;
        }
    }
    if (a->len == b->len) {
        return 0;
    }
    return (a->len < b->len) ? -1 : 1;
}

/* Igualdade de valores de qualquer tipo (tipos diferentes são sempre desiguais). */
static int values_equal(const struct value *a, const struct value *b)
{
    if (is_int_like(a) && is_int_like(b)) {
        return a->i == b->i;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    if (a->kind == V_STR) {
        return str_compare(a, b) == 0;
    }
    return 1;                           /* None == None */
}

/* Executa a função embutida 'fn' sobre os 'argc' valores no topo da pilha. */
static int call_builtin(int_out_fn out, uint8_t fn, uint8_t argc, uint32_t *sp,
                        uint32_t pc, struct int_error *err)
{
    if (*sp < argc) {
        int_set_error_hex(err, "pilha vazia na chamada na posicao", pc);
        return -1;
    }

    if (fn == BI_PRINT) {
        uint32_t base = *sp - argc;
        for (uint32_t i = 0; i < argc; i++) {
            if (i > 0) {
                emit(out, " ", 1);
            }
            emit_value(out, &stack[base + i]);
        }
        emit(out, "\n", 1);
        *sp = base;
        stack[(*sp)++] = make_none();
        return 0;
    }

    if (argc != 1) {
        int_set_error_hex(err, "numero de argumentos invalido na posicao", pc);
        return -1;
    }
    struct value *a = &stack[*sp - 1];
    struct value r;
    switch (fn) {
    case BI_LEN:
        if (a->kind != V_STR) {
            int_set_error(err, 0, "len() exige uma string");
            return -1;
        }
        r = make_int((int32_t)a->len);
        break;
    case BI_STR:
        if (a->kind == V_STR) {
            r = *a;
        } else if (is_int_like(a) && a->kind == V_INT) {
            char buf[12];
            size_t n = format_int(buf, a->i);
            char *dst = arena_alloc(n);
            if (dst == NULL) {
                int_set_error(err, 0, "memoria de strings esgotada");
                return -1;
            }
            for (size_t k = 0; k < n; k++) {
                dst[k] = buf[k];
            }
            r = make_str(dst, (uint32_t)n);
        } else {
            struct value tmp = *a;
            char buf[8];
            size_t n = 0;
            if (tmp.kind == V_BOOL) {
                const char *t = tmp.i ? "True" : "False";
                for (; t[n] != '\0'; n++) {
                    buf[n] = t[n];
                }
            } else {
                buf[n++] = 'N';
                buf[n++] = 'o';
                buf[n++] = 'n';
                buf[n++] = 'e';
            }
            char *dst = arena_alloc(n);
            if (dst == NULL) {
                int_set_error(err, 0, "memoria de strings esgotada");
                return -1;
            }
            for (size_t k = 0; k < n; k++) {
                dst[k] = buf[k];
            }
            r = make_str(dst, (uint32_t)n);
        }
        break;
    case BI_INT:
        if (a->kind == V_STR) {
            int32_t v;
            int rc = parse_int(a->s, a->len, &v);
            if (rc == -1) {
                int_set_error(err, 0, "int() com texto que nao e numero");
                return -1;
            }
            if (rc == -2) {
                int_set_error(err, 0, "int() fora do intervalo de 32 bits");
                return -1;
            }
            r = make_int(v);
        } else if (is_int_like(a)) {
            r = make_int(a->i);
        } else {
            int_set_error(err, 0, "int() exige numero ou string");
            return -1;
        }
        break;
    default:
        int_set_error(err, 0, "funcao embutida desconhecida");
        return -1;
    }
    *a = r;
    return 0;
}

int int_run(const uint8_t *img, size_t len, int_out_fn out,
            int (*should_stop)(void), struct int_error *err)
{
    struct int_info info;
    if (int_verify(img, len, &info, err) != 0) {
        return -1;
    }

    const uint8_t *code = img + INT_HEADER_SIZE;
    const uint8_t *cst = code + info.code_size;

    arena_used = 0;
    for (uint32_t k = 0; k < INT_MAX_SLOTS; k++) {
        slots[k].kind = V_UNBOUND;
        slots[k].i = 0;
        slots[k].s = NULL;
        slots[k].len = 0;
    }

    uint32_t sp = 0;
    uint32_t pc = 0;
    uint32_t steps = 0;

#define PUSH(v)                                                              \
    do {                                                                     \
        if (sp >= STACK_MAX) {                                               \
            int_set_error_hex(err, "pilha de execucao cheia na posicao", pc); \
            return -1;                                                       \
        }                                                                    \
        stack[sp++] = (v);                                                   \
    } while (0)

#define NEED(n)                                                              \
    do {                                                                     \
        if (sp < (n)) {                                                      \
            int_set_error_hex(err, "pilha vazia na posicao", pc);            \
            return -1;                                                       \
        }                                                                    \
    } while (0)

#define BIN_INT(expr_op, label)                                              \
    do {                                                                     \
        NEED(2);                                                             \
        struct value *b_ = &stack[sp - 1];                                   \
        struct value *a_ = &stack[sp - 2];                                   \
        if (!is_int_like(a_) || !is_int_like(b_)) {                          \
            int_set_error_hex(err, label " exige inteiros na posicao", pc);  \
            return -1;                                                       \
        }                                                                    \
        a_->kind = V_INT;                                                    \
        a_->i = (expr_op);                                                   \
        sp--;                                                                \
    } while (0)

#define CMP(cond)                                                            \
    do {                                                                     \
        NEED(2);                                                             \
        struct value *b_ = &stack[sp - 1];                                   \
        struct value *a_ = &stack[sp - 2];                                   \
        int r_;                                                              \
        if (is_int_like(a_) && is_int_like(b_)) {                            \
            r_ = (a_->i cond b_->i);                                         \
        } else if (a_->kind == V_STR && b_->kind == V_STR) {                 \
            int c_ = str_compare(a_, b_);                                    \
            r_ = (c_ cond 0);                                                \
        } else {                                                             \
            int_set_error_hex(err, "comparacao entre tipos invalidos na posicao", pc); \
            return -1;                                                       \
        }                                                                    \
        *a_ = make_bool(r_);                                                 \
        sp--;                                                                \
    } while (0)

    for (;;) {
        if (should_stop != NULL && (++steps % STOP_EVERY) == 0 && should_stop()) {
            int_set_error_hex(err, "execucao interrompida na posicao", pc);
            return -1;
        }

        uint8_t op = code[pc];
        switch (op) {
        case OP_HALT:
            return 0;

        case OP_PUSH_INT:
            PUSH(make_int((int32_t)int_rd32(code + pc + 1)));
            break;

        case OP_PUSH_STR: {
            uint32_t off = int_rd32(code + pc + 1);
            uint16_t n = int_rd16(code + pc + 5);
            PUSH(make_str((const char *)(cst + off), n));
            break;
        }

        case OP_LOAD: {
            uint8_t slot = code[pc + 1];
            if (slots[slot].kind == V_UNBOUND) {
                int_set_error_hex(err, "variavel usada antes de receber valor, posicao", pc);
                return -1;
            }
            PUSH(slots[slot]);
            break;
        }

        case OP_STORE: {
            NEED(1);
            slots[code[pc + 1]] = stack[--sp];
            break;
        }

        case OP_POP:
            NEED(1);
            sp--;
            break;

        case OP_ADD: {
            NEED(2);
            struct value *b = &stack[sp - 1];
            struct value *a = &stack[sp - 2];
            if (a->kind == V_STR && b->kind == V_STR) {
                char *dst = arena_alloc(a->len + b->len);
                if (dst == NULL) {
                    int_set_error(err, 0, "memoria de strings esgotada");
                    return -1;
                }
                for (uint32_t k = 0; k < a->len; k++) {
                    dst[k] = a->s[k];
                }
                for (uint32_t k = 0; k < b->len; k++) {
                    dst[a->len + k] = b->s[k];
                }
                *a = make_str(dst, a->len + b->len);
                sp--;
            } else {
                BIN_INT((int32_t)((uint32_t)a_->i + (uint32_t)b_->i), "+");
            }
            break;
        }

        case OP_SUB:
            BIN_INT((int32_t)((uint32_t)a_->i - (uint32_t)b_->i), "-");
            break;

        case OP_MUL:
            BIN_INT((int32_t)((uint32_t)a_->i * (uint32_t)b_->i), "*");
            break;

        case OP_FLOORDIV:
        case OP_MOD: {
            NEED(2);
            struct value *b = &stack[sp - 1];
            struct value *a = &stack[sp - 2];
            if (!is_int_like(a) || !is_int_like(b)) {
                int_set_error_hex(err, "// e % exigem inteiros na posicao", pc);
                return -1;
            }
            if (b->i == 0) {
                int_set_error_hex(err, "divisao por zero na posicao", pc);
                return -1;
            }
            int32_t x = a->i;
            int32_t y = b->i;
            int32_t q;
            int32_t r;
            if (x == (int32_t)0x80000000 && y == -1) {
                /* único caso que estoura: o resultado volta a -2^31 (wrap) */
                q = (int32_t)0x80000000;
                r = 0;
            } else {
                q = x / y;
                r = x % y;
                /* Semântica do Python: divisão para baixo e resto com sinal do divisor. */
                if (r != 0 && ((r < 0) != (y < 0))) {
                    q--;
                    r += y;
                }
            }
            a->kind = V_INT;
            a->i = (op == OP_FLOORDIV) ? q : r;
            sp--;
            break;
        }

        case OP_NEG: {
            NEED(1);
            struct value *a = &stack[sp - 1];
            if (!is_int_like(a)) {
                int_set_error_hex(err, "- unario exige inteiro na posicao", pc);
                return -1;
            }
            a->kind = V_INT;
            a->i = (int32_t)(0u - (uint32_t)a->i);
            break;
        }

        case OP_NOT: {
            NEED(1);
            struct value *a = &stack[sp - 1];
            *a = make_bool(!truthy(a));
            break;
        }

        case OP_EQ:
        case OP_NE: {
            NEED(2);
            struct value *b = &stack[sp - 1];
            struct value *a = &stack[sp - 2];
            int eq = values_equal(a, b);
            *a = make_bool(op == OP_EQ ? eq : !eq);
            sp--;
            break;
        }

        case OP_LT:
            CMP(<);
            break;
        case OP_LE:
            CMP(<=);
            break;
        case OP_GT:
            CMP(>);
            break;
        case OP_GE:
            CMP(>=);
            break;

        case OP_JMP:
            pc = int_rd32(code + pc + 1);
            continue;

        case OP_JF: {
            NEED(1);
            uint32_t target = int_rd32(code + pc + 1);
            if (!truthy(&stack[--sp])) {
                pc = target;
                continue;
            }
            break;
        }

        case OP_JT_KEEP:
        case OP_JF_KEEP: {
            NEED(1);
            uint32_t target = int_rd32(code + pc + 1);
            int t = truthy(&stack[sp - 1]);
            int jump = (op == OP_JT_KEEP) ? t : !t;
            if (jump) {
                pc = target;
                continue;
            }
            sp--;
            break;
        }

        case OP_CALL: {
            uint8_t fn = code[pc + 1];
            uint8_t argc = code[pc + 2];
            if (call_builtin(out, fn, argc, &sp, pc, err) != 0) {
                return -1;
            }
            break;
        }

        default:
            int_set_error_hex(err, "opcode invalido na posicao", pc);
            return -1;
        }

        pc += 1 + int_operand_len[op];
    }

#undef PUSH
#undef NEED
#undef BIN_INT
#undef CMP
}
