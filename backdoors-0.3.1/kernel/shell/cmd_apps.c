/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>

#include "apps/editor.h"
#include "apps/files.h"
#include "kernel/log.h"
#include "shell/builtins.h"

void cmd_edit(int argc, char **argv)
{
    if (argc != 2) {
        kprintf("uso: edit ARQUIVO   (dentro do editor: :help)\n");
        return;
    }
    editor_run(argv[1]);
}

void cmd_files(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    files_run();
}
