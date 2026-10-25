/* SPDX-License-Identifier: GPL-3.0-or-later
 * Teste unitário do parser do shell (roda no host, não no kernel).
 * Compilar e rodar: sh tests/unit/run_unit.sh */
#include <stdio.h>
#include <string.h>
#include "shell/parser.h"

static int failures = 0;

static void expect_args(const char *input, int want_argc, const char *const want[])
{
    char line[128];
    strcpy(line, input);
    char *argv[16];
    int argc = parse_line(line, argv, 16);
    int ok = (argc == want_argc);
    for (int i = 0; ok && i < argc; i++) {
        ok = strcmp(argv[i], want[i]) == 0;
    }
    if (!ok) {
        failures++;
        printf("FAIL: [%s] argc=%d (esperado %d)\n", input, argc, want_argc);
    }
}

int main(void)
{
    expect_args("echo hi", 2, (const char *const[]){ "echo", "hi" });
    expect_args("echo hello   \"big   world\"", 3,
                (const char *const[]){ "echo", "hello", "big   world" });
    expect_args("nope", 1, (const char *const[]){ "nope" });
    expect_args("   ", 0, (const char *const[]){ NULL });
    expect_args("a  b", 2, (const char *const[]){ "a", "b" });

    char line[64];
    char *argv[4];
    strcpy(line, "echo \"sem fechar");
    if (parse_line(line, argv, 4) != -1) { failures++; printf("FAIL: aspas sem fechamento\n"); }
    strcpy(line, "a b c d e");
    if (parse_line(line, argv, 4) != -1) { failures++; printf("FAIL: argumentos demais\n"); }

    if (failures == 0) {
        printf("OK parser: 7 casos\n");
        return 0;
    }
    printf("%d falha(s)\n", failures);
    return 1;
}
