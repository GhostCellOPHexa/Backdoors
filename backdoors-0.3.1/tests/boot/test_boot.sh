#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Testes de boot (Fases 1 e 2). Cada caso gera uma ISO (com validação
# Multiboot), roda no QEMU sem janela e verifica serial (COM1) e VGA.
#
#   boot    : boot normal; GDT, IDT, PIC e timer funcionando.
#   panic   : falha fatal provocada (BD_TEST_PANIC).
#   int3    : breakpoint lançado e tratado; o kernel deve continuar.
#   div0    : divisão por zero; deve virar pânico 'Divide Error (#DE)'.

set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"

QEMU=${QEMU:-qemu-system-i386}
PYTHON=${PYTHON:-python3}
TIMEOUT=${TEST_TIMEOUT:-30}
OUT=build/test

# run_case <nome> <flag de build ou vazio> <checker args...>
run_case() {
    name=$1
    flag=$2
    shift 2

    build_dir=build/t-$name
    iso=$build_dir/backdoors.iso
    log_dir=$OUT/$name

    make -s BUILD="$build_dir" EXTRA_CFLAGS="$flag" iso

    mkdir -p "$log_dir"
    rm -f "$log_dir/serial.log" "$log_dir/monitor.log"

    # Espera o kernel terminar, despeja a VGA e encerra o QEMU. O timeout é
    # uma rede de segurança: um kernel travado falha pela verificação.
    ( sleep 5; printf 'xp /4000xb 0xb8000\nquit\n' ) |
        timeout "$TIMEOUT" "$QEMU" \
            -cdrom "$iso" \
            -display none \
            -m 32 \
            -no-reboot \
            -serial "file:$log_dir/serial.log" \
            -monitor stdio \
            > "$log_dir/monitor.log" 2>&1 || true

    echo "--- caso: $name"
    "$PYTHON" tests/boot/check_output.py "$@" "$log_dir/serial.log" "$log_dir/monitor.log"
}

run_case boot "" boot --require "GDT loaded (CS=0x8)" \
                      --require "IDT loaded, PIC remapped, PIT at 100 Hz" \
                      --require "[boot] timer: " \
                      --require "[boot] memory: " \
                      --require "heap 4096 KiB ready, self-test passed" \
                      --require "[boot] initramfs: " \
                      --require "[boot] phase 3: shell started"

run_case panic -DBD_TEST_PANIC panic --message "test panic requested at build time"

run_case int3 -DBD_TEST_INT3 boot --require "[test] breakpoint returned"

run_case div0 -DBD_TEST_DIV0 panic --message "Divide Error (#DE)"

echo "--- todos os casos de boot passaram"
