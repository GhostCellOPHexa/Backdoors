/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Testes do compilador Python -> .int, do verificador e da VM.
 * Compilado com o gcc do host: o código de lang/ não depende do kernel.
 */
#include <stdio.h>
#include <string.h>

#include "lang/int.h"
#include "lang/int_internal.h"

static int failures;
static int checks;

#define CHECK(cond, ...)                                          \
    do {                                                          \
        checks++;                                                 \
        if (!(cond)) {                                            \
            failures++;                                           \
            fprintf(stderr, "FALHA %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                         \
            fprintf(stderr, "\n");                                \
        }                                                         \
    } while (0)

static uint8_t image[INT_HEADER_SIZE + INT_MAX_CODE + INT_MAX_CONST];
static size_t image_len;
static char out_buf[4096];
static size_t out_len;

static void capture(const char *s, size_t n)
{
    if (out_len + n < sizeof(out_buf)) {
        memcpy(out_buf + out_len, s, n);
        out_len += n;
    }
    out_buf[out_len] = '\0';
}

static int stop_forever(void)
{
    return 1;
}

static int compile_src(const char *src, struct int_error *err)
{
    return int_compile_python(src, strlen(src), image, sizeof(image), &image_len, err);
}

/* Compila e executa. Retorna 0 se tudo deu certo; saída em out_buf. */
static int run_src(const char *src, struct int_error *err)
{
    out_len = 0;
    out_buf[0] = '\0';
    if (compile_src(src, err) != 0) {
        return -2;
    }
    return int_run(image, image_len, capture, NULL, err);
}

static void expect_output(const char *name, const char *src, const char *want)
{
    struct int_error err;
    int rc = run_src(src, &err);
    CHECK(rc == 0, "%s: rc=%d erro=%s (linha %d)", name, rc, err.msg, err.line);
    CHECK(strcmp(out_buf, want) == 0, "%s: saida [%s] esperada [%s]", name, out_buf, want);
}

static void expect_compile_error(const char *name, const char *src, int line, const char *frag)
{
    struct int_error err;
    int rc = compile_src(src, &err);
    CHECK(rc != 0, "%s: deveria falhar ao compilar", name);
    CHECK(err.line == line, "%s: linha %d esperada, veio %d (%s)", name, line, err.line, err.msg);
    CHECK(strstr(err.msg, frag) != NULL, "%s: mensagem '%s' sem '%s'", name, err.msg, frag);
}

static void expect_runtime_error(const char *name, const char *src, const char *frag)
{
    struct int_error err;
    int rc = run_src(src, &err);
    CHECK(rc == -1, "%s: deveria falhar em execucao (rc=%d)", name, rc);
    CHECK(strstr(err.msg, frag) != NULL, "%s: mensagem '%s' sem '%s'", name, err.msg, frag);
}

/* ---------- casos de saída ---------- */

static void test_print_and_arith(void)
{
    expect_output("print basico", "print(\"ola\", 1 + 2)\n", "ola 3\n");
    expect_output("precedencia", "print(2 + 3 * 4, (2 + 3) * 4)\n", "14 20\n");
    expect_output("floor div e mod (python)",
                  "print(-7 // 2, -7 % 2, 7 // -2, 7 % -2)\n", "-4 1 -4 -1\n");
    expect_output("overflow envolve",
                  "print(2147483647 + 1)\n", "-2147483648\n");
    expect_output("negativo", "x = 5\nprint(-x, - -x)\n", "-5 5\n");
}

static void test_bools_and_logic(void)
{
    expect_output("bool impresso", "print(1 == 1, 2 < 1, True, False)\n", "True False True False\n");
    expect_output("not", "print(not 0, not 3)\n", "True False\n");
    expect_output("or devolve operando", "print(0 or \"x\", 0 or 0)\n", "x 0\n");
    expect_output("and devolve operando", "print(1 and 0, 2 and 3)\n", "0 3\n");
    expect_output("curto-circuito and",
                  "x = 0\nif 0 and (1 // x):\n    print(\"nao\")\nelse:\n    print(\"ok\")\n",
                  "ok\n");
}

static void test_strings(void)
{
    expect_output("concat e len",
                  "print(\"ab\" + \"cd\", len(\"abc\"))\n", "abcd 3\n");
    expect_output("str e int",
                  "print(str(42) + \"!\", int(\"-17\") + 1)\n", "42! -16\n");
    expect_output("escapes", "print(\"a\\tb\")\n", "a\tb\n");
    expect_output("comparacao de texto", "print(\"abc\" < \"abd\", \"a\" == \"a\")\n", "True True\n");
}

static void test_control_flow(void)
{
    expect_output("while e atribuicao aumentada",
                  "t = 0\ni = 1\nwhile i <= 10:\n    t += i\n    i += 1\nprint(t)\n",
                  "55\n");
    expect_output("for range um argumento",
                  "for i in range(3):\n    print(i)\n", "0\n1\n2\n");
    expect_output("for range dois argumentos",
                  "for i in range(2, 5):\n    print(i)\n", "2\n3\n4\n");
    expect_output("for vazio", "for i in range(5, 2):\n    print(i)\nprint(\"fim\")\n", "fim\n");
    expect_output("break e continue",
                  "for i in range(10):\n"
                  "    if i == 2:\n"
                  "        continue\n"
                  "    if i == 4:\n"
                  "        break\n"
                  "    print(i)\n",
                  "0\n1\n3\n");
    expect_output("while com break",
                  "n = 0\nwhile True:\n    n += 1\n    if n == 3:\n        break\nprint(n)\n",
                  "3\n");
    expect_output("elif e else",
                  "x = 7\nif x < 5:\n    print(\"a\")\nelif x < 10:\n    print(\"b\")\nelse:\n    print(\"c\")\n",
                  "b\n");
    expect_output("else de linha unica",
                  "x = 1\nif x == 0: print(\"zero\")\nelse: print(\"nao zero\")\n",
                  "nao zero\n");
    expect_output("comentarios e linhas vazias",
                  "# comentario\n\nx = 2  # outro\n\nprint(x)\n", "2\n");
    expect_output("parenteses em varias linhas",
                  "print(1,\n      2)\n", "1 2\n");
}

static void test_program_files(void)
{
    expect_output("somas.py (do initramfs)",
                  "total = 0\ni = 1\nwhile i <= 10:\n    total += i\n    i += 1\n"
                  "print(\"soma de 1 a 10 =\", total)\n"
                  "if total == 55:\n    print(\"correto\")\nelse:\n    print(\"errado\")\n",
                  "soma de 1 a 10 = 55\ncorreto\n");
}

/* ---------- erros ---------- */

static void test_compile_errors(void)
{
    expect_compile_error("def nao suportado", "def f():\n    pass\n", 1, "nao suportado");
    expect_compile_error("nome nao definido", "print(y)\n", 1, "nome nao definido");
    expect_compile_error("break fora de laco", "x = 1\nbreak\n", 2, "fora de laco");
    expect_compile_error("tabulacao", "if 1:\n\tprint(1)\n", 2, "tabulacao");
    expect_compile_error("string aberta", "print(\"oi)\n", 1, "sem fechamento");
    expect_compile_error("indentacao ruim",
                         "if 1:\n        x = 1\n    y = 2\n", 3, "indentacao");
    expect_compile_error("atribuicao sem valor", "x = \n", 1, "expressao");
    expect_compile_error("bloco esperado", "if 1:\nprint(1)\n", 2, "indentado");
    expect_compile_error("comparacao encadeada", "print(1 < 2 < 3)\n", 1, "encadeada");
    expect_compile_error("funcao desconhecida", "foo(1)\n", 1, "funcao nao suportada");
    expect_compile_error("len com dois argumentos", "print(len(\"a\", \"b\"))\n", 1, "um argumento");
    expect_compile_error("numero enorme", "x = 99999999999\n", 1, "grande demais");
    expect_compile_error("caractere estranho", "x = 1 @ 2\n", 1, "caractere");
}

static void test_runtime_errors(void)
{
    expect_runtime_error("divisao por zero", "print(1 // 0)\n", "divisao por zero");
    expect_runtime_error("modulo por zero", "x = 0\nprint(5 % x)\n", "divisao por zero");
    expect_runtime_error("soma de texto e numero", "print(\"a\" + 1)\n", "exige inteiros");
    expect_runtime_error("len de numero", "print(len(5))\n", "len()");
    expect_runtime_error("int invalido", "print(int(\"abc\"))\n", "nao e numero");
    expect_runtime_error("int fora de faixa", "print(int(\"99999999999\"))\n", "intervalo");
}

static void test_interrupt(void)
{
    struct int_error err;
    CHECK(compile_src("while True:\n    pass\n", &err) == 0, "compila loop infinito");
    int rc = int_run(image, image_len, capture, stop_forever, &err);
    CHECK(rc == -1 && strstr(err.msg, "interrompida") != NULL,
          "loop infinito interrompido (rc=%d msg=%s)", rc, err.msg);
}

/* ---------- imagens .int adulteradas ---------- */

static int recompute_checksum(void)
{
    uint32_t code_size = int_rd32(image + 8);
    uint32_t const_size = int_rd32(image + 12);
    uint32_t sum = int_fnv1a(image + INT_HEADER_SIZE, code_size + const_size);
    for (int b = 0; b < 4; b++) {
        image[20 + b] = (uint8_t)((sum >> (8 * b)) & 0xFF);
    }
    return 0;
}

static void expect_verify_fails(const char *name, struct int_error *err, const char *frag)
{
    int rc = int_verify(image, image_len, NULL, err);
    CHECK(rc != 0, "%s: verificador deveria rejeitar", name);
    CHECK(strstr(err->msg, frag) != NULL, "%s: mensagem '%s' sem '%s'", name, err->msg, frag);
}

static void test_verifier(void)
{
    struct int_error err;
    const char *src = "x = 1\nwhile x < 3:\n    x += 1\nprint(x)\n";
    CHECK(compile_src(src, &err) == 0, "compila para teste do verificador");
    struct int_info info;
    CHECK(int_verify(image, image_len, &info, &err) == 0, "imagem valida passa: %s", err.msg);
    CHECK(info.instructions > 3, "contagem de instrucoes");
    CHECK(info.slots == 1, "um slot: %u", info.slots);

    /* Truncada: o tamanho não confere. */
    int rc = int_verify(image, image_len - 1, NULL, &err);
    CHECK(rc != 0, "truncada rejeitada");

    /* Magic errado. */
    image[0] = 'X';
    expect_verify_fails("magic", &err, "magic");
    image[0] = INT_MAGIC_0;

    /* Byte do código alterado sem corrigir checksum. */
    image[INT_HEADER_SIZE] ^= 0x55;
    expect_verify_fails("checksum", &err, "checksum");
    image[INT_HEADER_SIZE] ^= 0x55;

    /* Opcode inválido, com checksum corrigido. */
    uint8_t saved = image[INT_HEADER_SIZE];
    image[INT_HEADER_SIZE] = 0xEE;
    recompute_checksum();
    expect_verify_fails("opcode", &err, "opcode invalido");
    image[INT_HEADER_SIZE] = saved;
    recompute_checksum();

    /* Salto para o meio de uma instrução. Localiza o primeiro JMP/JF. */
    uint32_t code_size = int_rd32(image + 8);
    uint32_t pc = 0;
    uint32_t jump_at = 0xFFFFFFFFu;
    while (pc < code_size) {
        uint8_t op = image[INT_HEADER_SIZE + pc];
        if (op == OP_JMP || op == OP_JF) {
            jump_at = pc;
            break;
        }
        pc += 1 + int_operand_len[op];
    }
    CHECK(jump_at != 0xFFFFFFFFu, "encontrou salto na imagem");
    if (jump_at != 0xFFFFFFFFu) {
        uint8_t *t = image + INT_HEADER_SIZE + jump_at + 1;
        uint32_t old = int_rd32(t);
        t[0] = (uint8_t)((old + 1) & 0xFF);   /* alvo deixa de ser início de instrução */
        recompute_checksum();
        expect_verify_fails("salto", &err, "salto invalido");
        t[0] = (uint8_t)(old & 0xFF);
        recompute_checksum();
    }

    /* Variável fora do limite (slot 200). */
    uint32_t slots_saved = int_rd32(image + 16);
    image[16] = 0; image[17] = 0;          /* nenhuma variável declarada */
    recompute_checksum();
    expect_verify_fails("slots", &err, "variavel fora do limite");
    image[16] = (uint8_t)slots_saved;
    recompute_checksum();

    CHECK(int_verify(image, image_len, NULL, &err) == 0, "imagem restaurada volta a valer");
}

int main(void)
{
    test_print_and_arith();
    test_bools_and_logic();
    test_strings();
    test_control_flow();
    test_program_files();
    test_compile_errors();
    test_runtime_errors();
    test_interrupt();
    test_verifier();

    if (failures != 0) {
        fprintf(stderr, "FALHA no teste da linguagem .int: %d de %d verificacoes\n",
                failures, checks);
        return 1;
    }
    printf("OK (unit int): %d verificacoes de compilador, VM e verificador\n", checks);
    return 0;
}
