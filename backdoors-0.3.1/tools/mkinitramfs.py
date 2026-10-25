#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Gera o initramfs do Backdoors OS: um arquivo cpio no formato "newc".

Uso: mkinitramfs.py DIRETORIO_DE_ORIGEM ARQUIVO_SAIDA

O GRUB carrega o arquivo como módulo e o kernel o copia para o sistema de
arquivos em memória no boot. A ordem é determinística (nomes ordenados e
tempo zero), então o mesmo diretório sempre gera o mesmo arquivo.
"""
import os
import sys

MODE_DIR = 0o040755
MODE_FILE = 0o100644


def pad4(n):
    return (4 - n % 4) % 4


def entry(name, mode, data):
    name_bytes = name.encode("ascii") + b"\0"
    fields = [
        0,                  # c_ino
        mode,               # c_mode
        0, 0,               # uid, gid
        1,                  # nlink
        0,                  # mtime
        len(data),          # c_filesize
        0, 0, 0, 0,         # devmajor, devminor, rdevmajor, rdevminor
        len(name_bytes),    # c_namesize
        0,                  # c_check
    ]
    header = b"070701" + b"".join(b"%08X" % f for f in fields)
    out = header + name_bytes + b"\0" * pad4(len(header) + len(name_bytes))
    out += data + b"\0" * pad4(len(data))
    return out


def collect(src):
    entries = []
    for root, dirs, files in os.walk(src):
        dirs.sort()
        rel_root = os.path.relpath(root, src)
        if rel_root != ".":
            entries.append((rel_root.replace(os.sep, "/"), MODE_DIR, b""))
        for name in sorted(files):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            with open(full, "rb") as f:
                entries.append((rel, MODE_FILE, f.read()))
    entries.sort(key=lambda e: e[0])
    return entries


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, out_path = sys.argv[1], sys.argv[2]
    if not os.path.isdir(src):
        print(f"mkinitramfs: diretório não encontrado: {src}", file=sys.stderr)
        return 1
    blob = b""
    for name, mode, data in collect(src):
        if len(name.encode("ascii")) > 4000:
            print(f"mkinitramfs: nome longo demais: {name}", file=sys.stderr)
            return 1
        blob += entry(name, mode, data)
    blob += entry("TRAILER!!!", 0, b"")
    with open(out_path, "wb") as f:
        f.write(blob)
    print(f"initramfs: {out_path} ({len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
