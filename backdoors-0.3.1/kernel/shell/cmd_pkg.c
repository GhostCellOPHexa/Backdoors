/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>

#include "kernel/log.h"
#include "pkg/pkg.h"
#include "shell/builtins.h"
#include "shell/script.h"

#define REPO_DIR "/repo"

static void pkg_usage(void)
{
    kprintf("uso: pkg install ARQUIVO.bkg\n");
    kprintf("     pkg remove NOME\n");
    kprintf("     pkg list\n");
    kprintf("     pkg info ARQUIVO.bkg\n");
    kprintf("     pkg update [DIRETORIO]   (padrao: %s)\n", REPO_DIR);
}

static int text_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

void cmd_pkg(int argc, char **argv)
{
    if (argc < 2) {
        pkg_usage();
        return;
    }
    const char *sub = argv[1];
    if (text_eq(sub, "install") && argc == 3) {
        pkg_install(argv[2]);
    } else if (text_eq(sub, "remove") && argc == 3) {
        pkg_remove(argv[2]);
    } else if (text_eq(sub, "list") && argc == 2) {
        pkg_list();
    } else if (text_eq(sub, "info") && argc == 3) {
        pkg_info(argv[2]);
    } else if (text_eq(sub, "update") && argc <= 3) {
        pkg_update(argc == 3 ? argv[2] : REPO_DIR);
    } else {
        pkg_usage();
    }
}

void cmd_sh(int argc, char **argv)
{
    if (argc != 2) {
        kprintf("uso: sh ARQUIVO\n");
        return;
    }
    script_run_file(argv[1]);
}

void cmd_set(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        kprintf("uso: set NOME VALOR   (use depois como $NOME)\n");
        return;
    }
    const char *value = (argc == 3) ? argv[2] : "";
    if (script_set(argv[1], value) != 0) {
        kprintf("set: nome invalido (letras, digitos e _; ate 15) ou 16 variaveis em uso\n");
    }
}
