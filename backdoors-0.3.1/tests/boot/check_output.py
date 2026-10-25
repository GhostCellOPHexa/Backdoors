#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verifica a saída de boot do Backdoors OS.

Modos:
  boot   SERIAL MONITOR [--require TEXTO]...
         Exige as 4 linhas do banner, na ordem, na serial e na VGA, e que
         cada TEXTO apareça em alguma linha da serial. Sem KERNEL PANIC.
  panic  SERIAL MONITOR --message MSG
         Exige 'KERNEL PANIC: MSG' e 'System halted.' na serial e na VGA,
         e proíbe o banner de sucesso.

SERIAL: saída da COM1 capturada pelo QEMU (-serial file:...).
MONITOR: saída do monitor HMP com `xp /4000xb 0xb8000` (memória VGA:
         80x25 células de 2 bytes, caractere + atributo).

Retorna 0 se tudo passar, 1 se alguma verificação falhar, 2 em erro de uso.
"""
import argparse
import re
import sys

BANNER = [
    "BACKDOORS OS",
    "Version 0.3.1",
    "Kernel initialized.",
    "System ready.",
]

VGA_COLS = 80
VGA_ROWS = 25

# Linha típica do HMP: "00000000000b8000: 0x42 0x07 0x41 ..." (0x opcional).
DUMP_LINE = re.compile(
    r"^\s*[0-9a-fA-F]+:\s+((?:(?:0x)?[0-9a-fA-F]{1,2})(?:\s+(?:0x)?[0-9a-fA-F]{1,2})*)\s*$"
)


def read_serial_lines(path):
    with open(path, "rb") as f:
        return [l.rstrip(b"\r").decode("ascii", "replace") for l in f.read().splitlines()]


def read_vga_rows(path):
    data = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = DUMP_LINE.match(line)
            if m:
                data += [int(tok, 16) for tok in m.group(1).split()]

    needed = VGA_COLS * VGA_ROWS * 2
    if len(data) < needed:
        raise ValueError(f"monitor: despejo VGA incompleto ({len(data)} de {needed} bytes)")

    chars = data[0:needed:2]
    rows = []
    for r in range(VGA_ROWS):
        row = chars[r * VGA_COLS:(r + 1) * VGA_COLS]
        rows.append("".join(chr(c) if 32 <= c < 127 else " " for c in row).rstrip())
    return rows


def in_order(lines, expected, label):
    errors, positions = [], []
    for want in expected:
        if want not in lines:
            errors.append(f"{label}: linha ausente: {want!r}")
        else:
            positions.append(lines.index(want))
    if positions and positions != sorted(positions):
        errors.append(f"{label}: linhas esperadas fora de ordem")
    return errors


def has_block(rows, expected):
    for start in range(VGA_ROWS - len(expected) + 1):
        if all(rows[start + k] == expected[k] for k in range(len(expected))):
            return True
    return False


def preview(rows, n=6):
    return "\n".join(f"      {i:2d}: {r}" for i, r in enumerate(rows[:n]))


def check_boot(serial_path, monitor_path, requires):
    lines = read_serial_lines(serial_path)
    errors = []
    if any("KERNEL PANIC" in l for l in lines):
        errors.append("serial: o kernel reportou KERNEL PANIC")
    errors += in_order(lines, BANNER, "serial")
    for text in requires:
        if not any(text in l for l in lines):
            errors.append(f"serial: texto ausente: {text!r}")

    try:
        rows = read_vga_rows(monitor_path)
    except ValueError as e:
        errors.append(str(e))
    else:
        if not has_block(rows, BANNER):
            errors.append("vga: banner não encontrado. Primeiras linhas:\n" + preview(rows))
    return errors


def check_panic(serial_path, monitor_path, message):
    line = f"KERNEL PANIC: {message}"
    expected = [line, "System halted."]
    lines = read_serial_lines(serial_path)
    errors = in_order(lines, expected, "serial")
    if "System ready." in lines:
        errors.append("serial: 'System ready.' apareceu apesar do pânico")

    try:
        rows = read_vga_rows(monitor_path)
    except ValueError as e:
        errors.append(str(e))
    else:
        if not has_block(rows, expected):
            errors.append("vga: pânico não encontrado. Primeiras linhas:\n" + preview(rows))
        if "System ready." in rows:
            errors.append("vga: 'System ready.' apareceu apesar do pânico")
    return errors


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=["boot", "panic"])
    parser.add_argument("serial")
    parser.add_argument("monitor")
    parser.add_argument("--require", action="append", default=[], metavar="TEXTO")
    parser.add_argument("--message", metavar="MSG")
    args = parser.parse_args(argv)

    if args.mode == "panic" and not args.message:
        parser.error("modo panic exige --message")

    try:
        if args.mode == "boot":
            errors = check_boot(args.serial, args.monitor, args.require)
        else:
            errors = check_panic(args.serial, args.monitor, args.message)
    except OSError as e:
        print(f"FALHA no teste ({args.mode}): não foi possível ler um log: {e}")
        return 1

    if errors:
        print(f"FALHA no teste ({args.mode}):")
        for e in errors:
            print(f"  - {e}")
        return 1

    if args.mode == "boot":
        print("OK (boot): banner na serial e na VGA, na ordem certa."
              + (f" Textos extras: {len(args.require)}." if args.require else ""))
    else:
        print(f"OK (panic): 'KERNEL PANIC: {args.message}' na serial e na VGA; kernel parou.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
