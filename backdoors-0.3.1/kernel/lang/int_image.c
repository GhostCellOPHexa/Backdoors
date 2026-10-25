/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdint.h>

#include "lang/int.h"
#include "lang/int_internal.h"

/* Operandos de cada opcode, em bytes (ver int.h). */
const uint8_t int_operand_len[OP_COUNT] = {
    [OP_HALT]      = 0,
    [OP_PUSH_INT]  = 4,
    [OP_PUSH_STR]  = 6,
    [OP_LOAD]      = 1,
    [OP_STORE]     = 1,
    [OP_POP]       = 0,
    [OP_ADD]       = 0,
    [OP_SUB]       = 0,
    [OP_MUL]       = 0,
    [OP_FLOORDIV]  = 0,
    [OP_MOD]       = 0,
    [OP_NEG]       = 0,
    [OP_NOT]       = 0,
    [OP_EQ]        = 0,
    [OP_NE]        = 0,
    [OP_LT]        = 0,
    [OP_LE]        = 0,
    [OP_GT]        = 0,
    [OP_GE]        = 0,
    [OP_JMP]       = 4,
    [OP_JF]        = 4,
    [OP_JT_KEEP]   = 4,
    [OP_JF_KEEP]   = 4,
    [OP_CALL]      = 2,
};

static const char *const op_names[OP_COUNT] = {
    "HALT", "PUSH_INT", "PUSH_STR", "LOAD", "STORE", "POP",
    "ADD", "SUB", "MUL", "FLOORDIV", "MOD", "NEG", "NOT",
    "EQ", "NE", "LT", "LE", "GT", "GE",
    "JMP", "JF", "JT_KEEP", "JF_KEEP", "CALL",
};

const char *int_op_name(uint8_t op)
{
    return (op < OP_COUNT) ? op_names[op] : "?";
}

uint16_t int_rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t int_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void int_set_error(struct int_error *err, int line, const char *msg)
{
    if (err == NULL) {
        return;
    }
    err->line = line;
    size_t i = 0;
    while (msg[i] != '\0' && i + 1 < sizeof(err->msg)) {
        err->msg[i] = msg[i];
        i++;
    }
    err->msg[i] = '\0';
}

/* Mensagem "texto 0xHEX" (para offsets e valores na verificação). */
void int_set_error_hex(struct int_error *err, const char *msg, uint32_t value)
{
    char buf[sizeof(err->msg)];
    size_t i = 0;
    for (; msg[i] != '\0' && i + 12 < sizeof(buf); i++) {
        buf[i] = msg[i];
    }
    buf[i++] = ' ';
    buf[i++] = '0';
    buf[i++] = 'x';
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t d = (value >> shift) & 0xFu;
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    buf[i] = '\0';
    int_set_error(err, 0, buf);
}

uint32_t int_fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* Bitmap: bit i = 1 se há início de instrução no byte i do código. */
static uint8_t boundary[INT_MAX_CODE / 8];

static int bit_get(const uint8_t *bits, uint32_t i)
{
    return (bits[i >> 3] >> (i & 7)) & 1;
}

static void bit_set(uint8_t *bits, uint32_t i)
{
    bits[i >> 3] |= (uint8_t)(1u << (i & 7));
}

/* Valida o operando de uma instrução já decodificada. Retorna 0 ou -1. */
static int check_operands(const uint8_t *code, uint32_t pc, uint8_t op,
                          uint32_t slots, uint32_t const_size, struct int_error *err)
{
    switch (op) {
    case OP_LOAD:
    case OP_STORE:
        if (code[pc + 1] >= slots) {
            int_set_error_hex(err, "variavel fora do limite na posicao", pc);
            return -1;
        }
        break;
    case OP_PUSH_STR: {
        uint32_t off = int_rd32(code + pc + 1);
        uint16_t len = int_rd16(code + pc + 5);
        if (off > const_size || (uint32_t)len > const_size - off) {
            int_set_error_hex(err, "constante fora do limite na posicao", pc);
            return -1;
        }
        break;
    }
    case OP_CALL: {
        uint8_t fn = code[pc + 1];
        uint8_t argc = code[pc + 2];
        if (fn >= BI_COUNT) {
            int_set_error_hex(err, "funcao embutida invalida na posicao", pc);
            return -1;
        }
        if (fn != BI_PRINT && argc != 1) {
            int_set_error_hex(err, "numero de argumentos invalido na posicao", pc);
            return -1;
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

int int_verify(const uint8_t *img, size_t len, struct int_info *info, struct int_error *err)
{
    if (len < INT_HEADER_SIZE) {
        int_set_error(err, 0, "imagem menor que o cabecalho");
        return -1;
    }
    if (img[0] != INT_MAGIC_0 || img[1] != INT_MAGIC_1 ||
        img[2] != INT_MAGIC_2 || img[3] != INT_MAGIC_3) {
        int_set_error(err, 0, "nao e um arquivo .int (magic INT1 ausente)");
        return -1;
    }
    if (int_rd16(img + 4) != INT_VERSION) {
        int_set_error(err, 0, "versao de formato .int nao suportada");
        return -1;
    }
    if (int_rd16(img + 6) != INT_HEADER_SIZE) {
        int_set_error(err, 0, "tamanho de cabecalho invalido");
        return -1;
    }

    uint32_t code_size = int_rd32(img + 8);
    uint32_t const_size = int_rd32(img + 12);
    uint32_t slots = int_rd32(img + 16);
    uint32_t checksum = int_rd32(img + 20);

    if (code_size == 0 || code_size > INT_MAX_CODE) {
        int_set_error(err, 0, "tamanho de codigo invalido");
        return -1;
    }
    if (const_size > INT_MAX_CONST) {
        int_set_error(err, 0, "tamanho de constantes invalido");
        return -1;
    }
    if (slots > INT_MAX_SLOTS) {
        int_set_error(err, 0, "numero de variaveis invalido");
        return -1;
    }
    if ((size_t)code_size + const_size != len - INT_HEADER_SIZE) {
        int_set_error(err, 0, "tamanho do arquivo nao confere com o cabecalho");
        return -1;
    }
    if (int_fnv1a(img + INT_HEADER_SIZE, code_size + const_size) != checksum) {
        int_set_error(err, 0, "checksum invalido: arquivo corrompido");
        return -1;
    }

    const uint8_t *code = img + INT_HEADER_SIZE;

    /* Passo 1: decodifica tudo, marca inícios de instrução e checa operandos. */
    for (uint32_t i = 0; i < INT_MAX_CODE / 8; i++) {
        boundary[i] = 0;
    }
    uint32_t pc = 0;
    uint32_t count = 0;
    uint8_t last = 0xFF;
    while (pc < code_size) {
        uint8_t op = code[pc];
        if (op >= OP_COUNT) {
            int_set_error_hex(err, "opcode invalido na posicao", pc);
            return -1;
        }
        uint32_t n = int_operand_len[op];
        if (pc + 1 + n > code_size) {
            int_set_error_hex(err, "instrucao cortada na posicao", pc);
            return -1;
        }
        bit_set(boundary, pc);
        if (check_operands(code, pc, op, slots, const_size, err) != 0) {
            return -1;
        }
        last = op;
        pc += 1 + n;
        count++;
    }
    if (last != OP_HALT) {
        int_set_error(err, 0, "codigo deve terminar com HALT");
        return -1;
    }

    /* Passo 2: todo salto cai no início de uma instrução. */
    pc = 0;
    while (pc < code_size) {
        uint8_t op = code[pc];
        if (op == OP_JMP || op == OP_JF || op == OP_JT_KEEP || op == OP_JF_KEEP) {
            uint32_t target = int_rd32(code + pc + 1);
            if (target >= code_size || !bit_get(boundary, target)) {
                int_set_error_hex(err, "salto invalido na posicao", pc);
                return -1;
            }
        }
        pc += 1 + int_operand_len[op];
    }

    if (info != NULL) {
        info->code_size = code_size;
        info->const_size = const_size;
        info->slots = slots;
        info->instructions = count;
        info->checksum = checksum;
    }
    return 0;
}
