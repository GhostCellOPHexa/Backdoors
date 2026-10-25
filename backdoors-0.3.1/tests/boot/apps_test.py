#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Teste dos aplicativos (Fases 5 a 7): sistema de arquivos, Python, .int,
pacotes, scripts, editor e gerenciador de arquivos, numa única sessão do QEMU.

Uso: apps_test.py <imagem.iso> <diretório de saída>

Shell e linguagens são verificados pela saída serial, agrupada por comando
(cada bloco começa numa linha 'bd> comando'). Editor e gerenciador desenham
na VGA, então são verificados pelas capturas da memória de vídeo.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_session import LINE_DELAY, Session  # noqa: E402


def blocks_of(serial):
    """Divide a saída serial em blocos: [(comando, [linhas de saída])]."""
    result = []
    cur = None
    for raw in serial.split("\n"):
        line = raw.rstrip()
        if line.startswith("bd> "):
            cur = (line[4:], [])
            result.append(cur)
        elif cur is not None:
            cur[1].append(line)
    return result


class Checker:
    def __init__(self, serial, screens):
        self.blocks = blocks_of(serial)
        self.serial = serial
        self.screens = screens
        self.errors = []
        self.passed = 0

    def out(self, prefix, nth=0):
        """Linhas de saída do n-ésimo comando que começa com 'prefix' (ou [])."""
        hits = [lines for cmd, lines in self.blocks if cmd.startswith(prefix)]
        return hits[nth] if len(hits) > nth else []

    def check(self, label, ok, detail=""):
        if ok:
            self.passed += 1
        else:
            self.errors.append(f"{label}" + (f" ({detail})" if detail else ""))

    def has_line(self, label, lines, expected):
        self.check(label, expected in lines, f"esperado linha {expected!r}")

    def has_text(self, label, lines, needle):
        self.check(label, any(needle in l for l in lines), f"esperado {needle!r}")

    def screen_has(self, label, idx, needle):
        if idx is None or idx >= len(self.screens):
            self.check(label, False, f"captura {idx} ausente")
            return
        screen = self.screens[idx]
        self.check(label, any(needle in row for row in screen), f"esperado {needle!r} na tela")


def run_session(s):
    """Roteiro completo. As capturas da VGA são devolvidas como índices."""
    snaps = {}

    # --- Sistema de arquivos ------------------------------------------------
    s.line("mkdir /work")
    s.line("cd /work")
    s.line("pwd")
    s.line("write /work/a.txt linha um")
    s.line("append /work/a.txt linha dois")
    s.line("cat /work/a.txt")
    s.line("cp /work/a.txt /work/b.txt")
    s.line("mv /work/b.txt /work/c.txt")
    s.line("ls /work")
    s.line("cat /nao-existe")
    s.line("mkdir /work/dir")
    s.line("touch /work/dir/x")
    s.line("rm /work/dir")
    s.line("rmdir /work/dir")
    s.line("rm /work/dir/x")
    s.line("rmdir /work/dir")
    s.line("ls /work")
    s.line("ls -l /work")
    s.line("tree /work")

    # --- Python e .int ------------------------------------------------------
    s.line("python -c print(2+3*4)")
    s.line('python -c "print(1 +)"')
    s.line("python /home/somas.py")
    s.line("pyc /home/ola.py /work/ola.int")
    s.line("intinfo /work/ola.int")
    s.line("int /work/ola.int")
    s.line("intinfo /home/ola.py")

    # --- Variáveis e scripts ------------------------------------------------
    s.line("set nome Teste")
    s.line("echo Ola $nome")
    s.line("sh /home/demo.sh")

    # --- Pacotes ------------------------------------------------------------
    s.line("pkg list")
    s.line("pkg install /repo/hello-1.0.0.bkg")
    s.line("pkg list")
    s.line("pkg install /repo/hello-1.0.0.bkg")
    s.line("pkg info /repo/hello-1.0.0.bkg")
    s.line("ls /home")
    s.line("pkg remove hello")
    s.line("pkg list")

    # Conflito: arquivo do pacote já existe e não pertence a ele. Nada muda.
    s.line("write /home/bemvindo.py meu")
    s.line("pkg install /repo/hello-1.0.0.bkg")
    s.line("cat /home/bemvindo.py")
    s.line("rm /home/bemvindo.py")
    s.line("pkg list")

    s.line("pkg update")
    s.line("pkg update /nao-existe")
    s.line("pkg remove hello")

    # --- Editor (VGA) -------------------------------------------------------
    s.line("edit /work/teste.py")
    s.keys(["i", "x = 6", "<ENTER>", "print(x * 7)", "<ESC>", ":w", "<ENTER>"])
    snaps["gravou"] = s.snapshot()
    s.keys([":run", "<ENTER>"])
    time.sleep(0.8)
    s.keys(["a"])                       # qualquer tecla fecha a saída do :run
    time.sleep(0.4)
    s.keys([":int", "<ENTER>"])
    snaps["compilou"] = s.snapshot()
    s.keys([":q", "<ENTER>"])           # sem alterações: sai direto
    time.sleep(LINE_DELAY)

    # Erro de sintaxe no buffer: :int aponta a linha.
    s.line("edit /work/erro.py")
    s.keys(["i", "x = (", "<ESC>", ":int", "<ENTER>"])
    snaps["erro"] = s.snapshot()
    s.keys([":q!", "<ENTER>"])
    time.sleep(LINE_DELAY)

    # Alteração não gravada: :q recusa (E37).
    s.line("edit /work/teste.py")
    s.keys(["i", "y", "<ESC>", ":q", "<ENTER>"])
    snaps["e37"] = s.snapshot()
    s.keys([":q!", "<ENTER>"])
    time.sleep(LINE_DELAY)

    s.line("ls /work")
    s.line("cat /work/teste.py")
    s.line("int /work/teste.int")

    # --- Gerenciador de arquivos (VGA) --------------------------------------
    s.line("mkdir /work2")
    s.line("write /work2/alvo.txt conteudo")
    s.line("cd /work2")
    s.line("files")
    snaps["files0"] = s.snapshot()
    s.keys(["n", "novo.txt", "<ENTER>"])
    snaps["novo"] = s.snapshot()
    s.keys(["<HOME>", "j", "d"])        # cursor em alvo.txt (0 é '../')
    snaps["apagar"] = s.snapshot()
    s.keys(["s", "<ENTER>"])
    snaps["apagado"] = s.snapshot()
    s.keys(["p", "pasta1", "<ENTER>"])
    snaps["pasta"] = s.snapshot()
    s.keys(["q"])
    time.sleep(LINE_DELAY)
    s.line("ls /work2")

    return snaps


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    iso, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    s = Session(iso, os.path.join(out_dir, "serial.log"),
                os.path.join(out_dir, "monitor.log"))
    try:
        snaps = run_session(s)
    finally:
        s.close()

    serial = s.serial_text()
    c = Checker(serial, s.screens())

    # Sistema de arquivos
    c.has_line("pwd em /work", c.out("pwd"), "/work")
    c.has_text("write/append/cat", c.out("cat /work/a.txt"), "linha um")
    c.has_text("append acrescenta", c.out("cat /work/a.txt"), "linha dois")
    c.has_line("ls após cp e mv", c.out("ls /work", 0), "a.txt  c.txt")
    c.has_text("cat de arquivo inexistente",
               c.out("cat /nao-existe"), "cat: '/nao-existe': nao encontrado")
    c.has_text("rm recusa diretório",
               c.out("rm /work/dir"), "rm: '/work/dir' e um diretorio")
    c.has_text("rmdir recusa diretório não vazio",
               c.out("rmdir /work/dir", 0), "diretorio nao esta vazio")
    c.has_line("ls após rmdir", c.out("ls /work", 1), "a.txt  c.txt")
    c.check("ls -l mostra o arquivo",
            any(re.match(r"^-\s+\d+\s+a\.txt$", l) for l in c.out("ls -l /work")))
    c.has_line("tree", c.out("tree /work"), "/work")
    c.has_text("tree mostra arquivos", c.out("tree /work"), "|-- a.txt")

    # Python e .int
    c.has_line("python -c", c.out("python -c print(2+3"), "14")
    c.has_text("python: erro de compilação com linha",
               c.out("python -c \"print(1 +"), "python: linha 1: ")
    c.has_line("somas.py imprime a soma", c.out("python /home/somas.py"),
               "soma de 1 a 10 = 55")
    c.has_text("pyc grava o .int", c.out("pyc /home/ola.py"), "pyc: gravado /work/ola.int (")
    c.has_text("intinfo versão", c.out("intinfo /work/ola.int"),
               "formato .int versao 1, cabecalho 32 bytes")
    c.check("intinfo confere checksum",
            any(re.match(r"^checksum: 0x[0-9A-F]{8} \(confere\)$", l)
                for l in c.out("intinfo /work/ola.int")))
    c.has_text("int executa o .int", c.out("int /work/ola.int"), "contagem 3 de 3")
    c.has_text("intinfo recusa texto",
               c.out("intinfo /home/ola.py"), "intinfo: ")

    # Scripts
    c.has_line("echo com variável", c.out("echo Ola"), "Ola Teste")
    c.has_text("sh roda o script", c.out("sh /home/demo.sh"), "Ola, Backdoors!")
    c.has_line("sh: pwd do script", c.out("sh /home/demo.sh"), "/home")

    # Pacotes
    c.has_line("pkg list vazio no início", c.out("pkg list", 0), "nenhum pacote instalado")
    c.has_line("pkg install", c.out("pkg install /repo/hello", 0),
               "pkg: hello 1.0.0 instalado (2 arquivos)")
    c.has_line("pkg list com hello", c.out("pkg list", 1), "hello 1.0.0")
    c.has_line("pkg install repetido", c.out("pkg install /repo/hello", 1),
               "pkg: hello 1.0.0 ja esta instalado")
    c.has_text("pkg info", c.out("pkg info"), "nome: hello")
    c.has_text("pacote instalou o arquivo", c.out("ls /home"), "bemvindo.py")
    c.has_line("pkg remove", c.out("pkg remove hello"), "pkg: hello removido")
    c.has_line("pkg list após remover", c.out("pkg list", 2), "nenhum pacote instalado")
    c.has_text("conflito recusado",
               c.out("pkg install /repo/hello", 2),
               "pkg: conflito: /home/bemvindo.py ja existe e nao pertence a este pacote")
    c.has_text("conflito não altera nada",
               c.out("pkg install /repo/hello", 2), "pkg: instalacao cancelada, nada foi alterado")
    c.has_line("arquivo do usuário preservado", c.out("cat /home/bemvindo.py"), "meu")
    c.has_text("pkg update", c.out("pkg update"),
               "pkg: repositorio /repo: 1 pacote(s), 0 com erro")
    c.has_text("pkg update com diretório inexistente",
               c.out("pkg update /nao-existe"), "pkg: repositorio nao encontrado: /nao-existe")

    # Editor (VGA)
    c.screen_has("editor: volta ao modo normal", snaps.get("gravou"), "-- NORMAL --")
    c.screen_has("editor: :w grava", snaps.get("gravou"), "gravado: linhas 2")
    c.screen_has("editor: :int compila", snaps.get("compilou"), "compilado para .int, bytes:")
    c.screen_has("editor: erro aponta a linha", snaps.get("erro"), "erro linha 1")
    c.screen_has("editor: :q recusa com alterações", snaps.get("e37"), "E37")
    c.has_line("editor: :run executa o programa", [l.rstrip() for l in serial.split("\n")], "42")
    c.has_line("programa salvo roda", c.out("int /work/teste.int"), "42")
    c.check("erro.py não foi gravado",
            not any("erro.py" in l for l in c.out("ls /work", 2)))
    c.has_text("editor gravou o conteúdo", c.out("cat /work/teste.py"), "print(x * 7)")

    # Gerenciador de arquivos (VGA)
    c.screen_has("arquivos: cabeçalho", snaps.get("files0"), "Arquivos:  /work2")
    c.screen_has("arquivos: lista alvo.txt", snaps.get("files0"), "alvo.txt")
    c.screen_has("arquivos: novo arquivo", snaps.get("novo"), "arquivo criado")
    c.screen_has("arquivos: prompt apagar", snaps.get("apagar"), "Apagar alvo.txt? (s/n):")
    c.screen_has("arquivos: apagado", snaps.get("apagado"), "apagado")
    c.screen_has("arquivos: nova pasta", snaps.get("pasta"), "pasta criada")
    final = c.out("ls /work2")
    c.has_text("arquivos: novo.txt existe", final, "novo.txt")
    c.has_text("arquivos: pasta1 existe", final, "pasta1/")
    c.check("arquivos: alvo.txt foi apagado", not any("alvo.txt" in l for l in final))

    c.check("sem KERNEL PANIC", "KERNEL PANIC" not in serial)

    if c.errors:
        print("FALHA no teste dos aplicativos:")
        for e in c.errors:
            print(f"  - {e}")
        print(f"  ({c.passed} verificações passaram; logs em {out_dir})")
        return 1

    print(f"OK (apps): {c.passed} verificações de arquivos, Python, .int, pacotes, "
          "scripts, editor e gerenciador de arquivos.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
