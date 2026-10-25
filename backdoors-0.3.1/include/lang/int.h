/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_LANG_INT_H
#define BD_LANG_INT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Formato .int, versão 1 (Fase 6).
 *
 * Um arquivo .int é uma imagem de programa para a máquina virtual do sistema:
 *
 *   offset  tamanho  campo
 *   0       4        magic "INT1"
 *   4       2        versão do formato (1)
 *   6       2        tamanho do cabeçalho (32)
 *   8       4        tamanho do código (bytes)
 *   12      4        tamanho da tabela de constantes (bytes)
 *   16      4        número de variáveis (slots, até INT_MAX_SLOTS)
 *   20      4        checksum FNV-1a de código + constantes
 *   24      8        reservado (zero)
 *   32      n        código (bytecode)
 *   32+n    m        constantes (bytes de strings, sem terminador)
 *
 * Inteiros são little-endian. Toda imagem é verificada antes de executar:
 * magic, tamanhos, checksum, opcodes, operandos, saltos e constantes.
 */

#define INT_MAGIC_0       'I'
#define INT_MAGIC_1       'N'
#define INT_MAGIC_2       'T'
#define INT_MAGIC_3       '1'
#define INT_VERSION       1u
#define INT_HEADER_SIZE   32u
#define INT_MAX_CODE      32768u
#define INT_MAX_CONST     4096u
#define INT_MAX_SLOTS     64u

/* Opcodes. Operandos entre parênteses, em bytes, little-endian. */
enum int_op {
    OP_HALT = 0,
    OP_PUSH_INT,        /* (i32)           empilha inteiro */
    OP_PUSH_STR,        /* (u32 off, u16 len) empilha string da tabela de constantes */
    OP_LOAD,            /* (u8 slot)       empilha variável */
    OP_STORE,           /* (u8 slot)       desempilha para variável */
    OP_POP,             /* descarta o topo */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_FLOORDIV,
    OP_MOD,
    OP_NEG,
    OP_NOT,
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_JMP,             /* (u32 alvo)      salto incondicional */
    OP_JF,              /* (u32 alvo)      desempilha; salta se falso */
    OP_JT_KEEP,         /* (u32 alvo)      se verdadeiro: salta mantendo o topo; senão desempilha */
    OP_JF_KEEP,         /* (u32 alvo)      se falso: salta mantendo o topo; senão desempilha */
    OP_CALL,            /* (u8 função, u8 argc) chamada de função embutida */
    OP_COUNT
};

/* Funções embutidas (OP_CALL). */
enum int_builtin {
    BI_PRINT = 0,       /* print(a, b, ...): argc qualquer, retorna None */
    BI_LEN,             /* len(s) */
    BI_STR,             /* str(x) */
    BI_INT,             /* int(x) */
    BI_COUNT
};

/* Erro de compilação (line > 0) ou de imagem/execução (line == 0). */
struct int_error {
    int line;
    char msg[96];
};

/* Dados de uma imagem verificada. */
struct int_info {
    uint32_t code_size;
    uint32_t const_size;
    uint32_t slots;
    uint32_t instructions;
    uint32_t checksum;
};

/* Destino da saída de print: recebe 'n' bytes (sem terminador). */
typedef void (*int_out_fn)(const char *s, size_t n);

/*
 * Compila o código fonte (subconjunto de Python, ver docs/PYTHON_SUBSET.md)
 * para uma imagem .int em 'out' (capacidade 'cap'). Retorna 0 e o tamanho em
 * '*out_len', ou -1 e preenche 'err'.
 */
int int_compile_python(const char *src, size_t len, uint8_t *out, size_t cap,
                       size_t *out_len, struct int_error *err);

/* Verifica uma imagem. Retorna 0 e preenche 'info', ou -1 com 'err'. */
int int_verify(const uint8_t *img, size_t len, struct int_info *info, struct int_error *err);

/*
 * Executa uma imagem (verificada antes). 'out' recebe a saída de print.
 * 'should_stop' (pode ser NULL) é consultada periodicamente: se devolver
 * não zero, a execução é interrompida. Retorna 0 ou -1 com 'err'.
 */
int int_run(const uint8_t *img, size_t len, int_out_fn out,
            int (*should_stop)(void), struct int_error *err);

/* Nome de um opcode (para intinfo e diagnósticos). */
const char *int_op_name(uint8_t op);

#endif /* BD_LANG_INT_H */
