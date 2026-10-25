#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Gera um pacote .bkg do Backdoors OS (arquivo cpio "newc" com PKG/INFO).

Uso: mkpkg.py ORIGEM NOME VERSAO SAIDA.bkg [DESCRICAO]

ORIGEM é um diretório cujo conteúdo é instalado a partir da raiz '/'.
Exemplo: ORIGEM/home/ola.py vira /home/ola.py no sistema.
NOME: letras minúsculas, dígitos, '_' e '-' (até 24 caracteres).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkinitramfs import collect, entry  # noqa: E402

NAME_RE = re.compile(r"^[a-z0-9_-]{1,24}$")
VERSION_RE = re.compile(r"^[0-9A-Za-z._+-]{1,23}$")


def main():
    if len(sys.argv) not in (5, 6):
        print(__doc__)
        return 2
    src, name, version, out = sys.argv[1:5]
    desc = sys.argv[5] if len(sys.argv) == 6 else ""
    if not NAME_RE.match(name):
        print(f"mkpkg: nome invalido: {name}", file=sys.stderr)
        return 1
    if not VERSION_RE.match(version):
        print(f"mkpkg: versao invalida: {version}", file=sys.stderr)
        return 1
    if not os.path.isdir(src):
        print(f"mkpkg: diretorio nao encontrado: {src}", file=sys.stderr)
        return 1
    if "\n" in desc or len(desc) > 60:
        print("mkpkg: descricao com quebra de linha ou longa demais (limite 60)", file=sys.stderr)
        return 1

    info = f"nome={name}\nversao={version}\ndescricao={desc}\n".encode("ascii")
    blob = entry("PKG/INFO", 0o100644, info)
    for rel, mode, data in collect(src):
        if rel.startswith("/") or ".." in rel.split("/"):
            print(f"mkpkg: caminho proibido: {rel}", file=sys.stderr)
            return 1
        blob += entry(rel, mode, data)
    blob += entry("TRAILER!!!", 0, b"")
    with open(out, "wb") as f:
        f.write(blob)
    print(f"pacote: {out} ({name} {version}, {len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
