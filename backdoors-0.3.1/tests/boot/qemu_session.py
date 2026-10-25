# SPDX-License-Identifier: GPL-3.0-or-later
"""Sessão interativa do Backdoors OS no QEMU, usada pelos testes do shell e dos apps.

As teclas são enviadas pelo monitor do QEMU com `sendkey`, então o teste exercita
o caminho real: PS/2 -> IRQ1 -> fila de teclas -> aplicativo. A saída de texto
da COM1 vai para um arquivo (serial); a tela VGA é lida com `xp` no monitor e
interpretada por `read_screens`.
"""
import os
import re
import subprocess
import time

QEMU = os.environ.get("QEMU", "qemu-system-i386")
BOOT_WAIT = 5.0
KEY_DELAY = 0.04
LINE_DELAY = 0.4
EXIT_TIMEOUT = 20.0

# Nome do keycode no monitor do QEMU para caracteres que não são letras minúsculas
# ou dígitos. Letras maiúsculas viram 'shift-<minúscula>'.
KEYNAME = {
    " ": "spc", "\n": "ret", "\b": "backspace", "-": "minus", ".": "dot",
    "/": "slash", '"': "shift-apostrophe", "'": "apostrophe", "(": "shift-9",
    ")": "shift-0", "+": "shift-equal", "*": "shift-8", ":": "shift-semicolon",
    ";": "semicolon", ",": "comma", "=": "equal", "_": "shift-minus",
    "#": "shift-3", "<": "less", ">": "shift-less", "!": "shift-1",
    "?": "shift-slash", "%": "shift-5", "&": "shift-7", "@": "shift-2",
    "[": "bracket_left", "]": "bracket_right", "{": "shift-bracket_left", "$": "shift-4",
    "}": "shift-bracket_right",
}

# Teclas especiais, escritas nos roteiros como <NOME>.
SPECIAL = {
    "<ESC>": "esc", "<ENTER>": "ret", "<BS>": "backspace", "<TAB>": "tab",
    "<UP>": "up", "<DOWN>": "down", "<LEFT>": "left", "<RIGHT>": "right",
    "<HOME>": "home", "<END>": "end", "<PGUP>": "pgup", "<PGDN>": "pgdn",
    "<DEL>": "delete",
}

VGA_COLS = 80
VGA_ROWS = 25

_DUMP_START = re.compile(r"^\s*0*b8000:")
_DUMP_BYTES = re.compile(
    r"^\s*[0-9a-fA-F]+:\s+((?:(?:0x)?[0-9a-fA-F]{1,2})(?:\s+(?:0x)?[0-9a-fA-F]{1,2})*)\s*$"
)


class Session:
    """Uma execução do QEMU com o monitor e a COM1 redirecionados para arquivos."""

    def __init__(self, iso, serial_path, monitor_path):
        self.serial_path = serial_path
        self.monitor_path = monitor_path
        self._monitor = open(monitor_path, "wb")
        self._snapshots = 0
        self.proc = subprocess.Popen(
            [QEMU, "-cdrom", iso, "-display", "none", "-m", "32", "-no-reboot",
             "-serial", f"file:{serial_path}", "-monitor", "stdio"],
            stdin=subprocess.PIPE, stdout=self._monitor, stderr=subprocess.STDOUT)
        time.sleep(BOOT_WAIT)

    def _send(self, text):
        self.proc.stdin.write((text + "\n").encode())
        self.proc.stdin.flush()

    def _key(self, name):
        self._send(f"sendkey {name}")
        time.sleep(KEY_DELAY)

    def text(self, s):
        """Digita texto sem apertar Enter."""
        for ch in s:
            if ch.isupper():
                self._key("shift-" + ch.lower())
            else:
                self._key(KEYNAME.get(ch, ch))

    def line(self, s, wait=LINE_DELAY):
        """Digita uma linha e aperta Enter."""
        self.text(s)
        self._key("ret")
        time.sleep(wait)

    def keys(self, script):
        """Roteiro: texto cru, teclas <ESPECIAIS> e <SNAP> (captura da VGA).

        Cada elemento é um token. Texto puro é digitado sem Enter; use
        line() para linhas de shell. Retorna os índices das capturas feitas.
        """
        taken = []
        for tok in script:
            if tok == "<SNAP>":
                taken.append(self.snapshot())
            elif tok in SPECIAL:
                self._key(SPECIAL[tok])
                time.sleep(0.1)
            else:
                self.text(tok)
                time.sleep(0.1)
        return taken

    def snapshot(self):
        """Despeja a memória VGA (80x25 células). Retorna o índice da captura."""
        time.sleep(0.3)
        self._send("xp /4000xb 0xb8000")
        time.sleep(0.8)
        idx = self._snapshots
        self._snapshots += 1
        return idx

    def wait_exit(self, timeout=EXIT_TIMEOUT):
        """Espera o QEMU encerrar sozinho (ex.: após 'reboot'). Retorna o código."""
        return self.proc.wait(timeout=timeout)

    def close(self):
        """Encerra o QEMU (se ainda estiver rodando) e fecha os arquivos."""
        if self.proc.poll() is None:
            try:
                self._send("quit")
                self.proc.wait(timeout=10)
            except (subprocess.TimeoutExpired, BrokenPipeError, OSError):
                self.proc.kill()
                self.proc.wait()
        self._monitor.close()
        return self.proc.returncode

    def serial_text(self):
        """Saída serial inteira, sem CR, decodificada em ASCII."""
        with open(self.serial_path, "rb") as f:
            return f.read().decode("ascii", "replace").replace("\r", "")

    def screens(self):
        """Todas as capturas da VGA, na ordem em que foram feitas."""
        return read_screens(self.monitor_path)


def read_screens(monitor_path):
    """Converte a saída do monitor (dumps de xp) em telas de 25 linhas de texto.

    Retorna uma lista; cada tela é uma lista de VGA_ROWS strings sem espaços
    finais. Um dump incompleto (menos de 4000 bytes) é descartado.
    """
    dumps = []
    cur = None
    with open(monitor_path, "r", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if _DUMP_START.match(line):
                cur = []
                dumps.append(cur)
            # A linha inicial do dump também traz bytes (é o endereço 0xb8000).
            m = _DUMP_BYTES.match(line)
            if m and cur is not None:
                cur += [int(tok, 16) for tok in m.group(1).split()]

    screens = []
    needed = VGA_COLS * VGA_ROWS * 2
    for data in dumps:
        if len(data) < needed:
            continue
        chars = data[0:needed:2]
        rows = []
        for r in range(VGA_ROWS):
            cells = chars[r * VGA_COLS:(r + 1) * VGA_COLS]
            rows.append("".join(chr(c) if 32 <= c < 127 else " " for c in cells).rstrip())
        screens.append(rows)
    return screens


def render_backspace(raw):
    """Aplica \\b como um terminal: apaga o caractere anterior na linha atual."""
    lines = [[]]
    for ch in raw:
        if ch == "\n":
            lines.append([])
        elif ch == "\b":
            if lines[-1]:
                lines[-1].pop()
        else:
            lines[-1].append(ch)
    return "\n".join("".join(l) for l in lines)
