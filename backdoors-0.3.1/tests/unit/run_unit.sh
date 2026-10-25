#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Testes unitários que não dependem de hardware, compilados com o gcc do host.
# Testam lógica pura: parser do shell, compilador Python -> .int, verificador e VM.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
OUT=build/unit
mkdir -p "$OUT"
gcc -Wall -Wextra -Werror -Iinclude -o "$OUT/parser_test" \
    tests/unit/parser_test.c kernel/shell/parser.c
"$OUT/parser_test"
gcc -std=gnu11 -Wall -Wextra -Werror -Iinclude -o "$OUT/int_test" \
    tests/unit/int_test.c kernel/lang/int_image.c kernel/lang/int_vm.c kernel/lang/py_compile.c
"$OUT/int_test"
