#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Teste do shell (Fases 3 a 5): digita comandos no QEMU e verifica a saída serial.

Uso: shell_test.py <imagem.iso> <diretório de saída>

As teclas são enviadas pelo monitor do QEMU (ver qemu_session.py), então o teste
exercita o caminho real: PS/2 -> IRQ1 -> fila -> readline -> comando.
Ao final digita 'reboot'; o QEMU (com -no-reboot) deve encerrar sozinho.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_session import LINE_DELAY, Session, render_backspace  # noqa: E402


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    iso, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    s = Session(iso, os.path.join(out_dir, "serial.log"),
                os.path.join(out_dir, "monitor.log"))
    try:
        s.line("version")
        s.line('echo hello   "big   world"')
        s.line("nope")
        s.line("mem")
        s.line("tree")
        s.line("uptime")
        s.line("date")
        s.line("cpu")
        s.line("devices")
        s.line("panic-test")
        s.line("about")

        # Histórico: seta para cima recupera 'about' e Enter o executa de novo.
        s.keys(["<UP>"])
        time.sleep(0.1)
        s.keys(["<ENTER>"])
        time.sleep(LINE_DELAY)

        # Backspace: digita 'echx', apaga o 'x' e completa 'o hi' => 'echo hi'.
        s.keys(["echx", "<BS>", "o hi", "<ENTER>"])
        time.sleep(LINE_DELAY)

        s.line("clear")
        s.line("reboot")

        try:
            code = s.wait_exit()
        except Exception:
            print("FALHA: QEMU não encerrou após 'reboot'")
            return 1
    finally:
        s.close()

    raw = s.serial_text()
    text = render_backspace(raw)   # tela como o usuário veria (backspace aplicado)

    errors = []

    def expect(label, pattern, regex=False, minimum=1, source=None):
        source = text if source is None else source
        found = len(re.findall(pattern, source)) if regex else source.count(pattern)
        if found < minimum:
            errors.append(f"{label}: esperado >= {minimum}, obtido {found} ({pattern!r})")

    expect("version", "BACKDOORS OS 0.3.1 (i686)")
    expect("echo com aspas preservando espaços", "hello big   world")
    expect("comando inexistente", "nope: comando nao encontrado")
    expect("mem: memória física", r"fisica: \d+ KiB total, \d+ KiB livre", regex=True)
    expect("mem: heap", r"heap:\s+4096 KiB total, \d+ bytes em uso", regex=True)
    expect("tree da raiz", "\n|-- home/", minimum=1)
    expect("tree mostra o initramfs", "|   |-- ola.py")
    expect("uptime", r"up \d+:\d{2}:\d{2} \(\d+ ticks\)", regex=True)
    expect("date (RTC)", r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} \(RTC\)", regex=True)
    expect("cpu vendor", r"vendor: \S+", regex=True)
    expect("devices", r"serial\s+COM1", regex=True)
    expect("panic-test desabilitado", "panic-test: desabilitado nesta build")
    expect("histórico com seta para cima (about 2x)", "Backdoors OS 0.3.1: sistema", minimum=2)
    expect("backspace: linha final é 'echo hi'", "bd> echo hi\n")
    expect("backspace: 'echo hi' imprime 'hi'", "\nhi\n")
    expect("clear emite sequência ANSI", "\x1b[2J", source=raw)
    expect("reboot", "rebooting...")
    if "KERNEL PANIC" in text:
        errors.append("serial: KERNEL PANIC apareceu durante o teste do shell")

    if errors:
        print("FALHA no teste do shell:")
        for e in errors:
            print(f"  - {e}")
        print(f"  (QEMU exit={code}; logs em {out_dir})")
        return 1

    print("OK (shell): comandos, aspas, erro de comando, árvore, histórico, backspace, clear e reboot.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
