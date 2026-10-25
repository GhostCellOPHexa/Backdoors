# Backdoors OS

Sistema operacional livre, gratuito e open source, leve e modular, para computadores modestos.
Arquitetura do kernel: x86 de 32 bits (i686). Linguagens: C e Assembly NASM, e a linguagem `.int` com um subconjunto de Python.
Interface: terminal de texto, sem mouse e sem gráfica.

Licença do código próprio: GPL-3.0-or-later (veja [LICENSE](LICENSE)).
Licenças de terceiros: veja [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Versão atual: 0.3.1

| Fase | Descrição | Estado |
|------|-----------|--------|
| 1 | Boot Multiboot, serial COM1, VGA, ISO no QEMU | Concluída e testada (0.1.0) |
| 2 | GDT própria, IDT, exceções, PIC, timer PIT a 100 Hz | Concluída e testada (0.2.0) |
| 3 | Teclado PS/2, shell interativo, comandos básicos | Concluída e testada (0.2.0) |
| 4 | Memória: mapa do bootloader, alocador físico (bitmap), heap do kernel | Concluída e testada |
| 5 | Sistema de arquivos em memória (VFS) e initramfs | Concluída e testada (0.3.1) |
| 6 | Formato `.int`, máquina virtual e compilador Python | Concluída e testada (0.3.1) |
| 7 | Editor de texto (estilo vi), gerenciador de arquivos, pacotes e scripts | Concluída e testada (0.3.1) |
| 8 | Armazenamento persistente | Não iniciada |
| 9 | Multitarefa e isolamento | Não iniciada |
| 10 | Estabilização | Não iniciada |

## O que a 0.3.1 faz

- **Boot e kernel**: Multiboot com GRUB, GDT, IDT, PIC, timer a 100 Hz, teclado PS/2 com setas, memória física e heap de 4 MiB com `kmalloc`/`kfree`.
- **Sistema de arquivos em memória**: árvore criada no boot a partir do initramfs (um arquivo cpio carregado pelo GRUB). Nomes de até 31 caracteres, arquivos de até 64 KiB. As mudanças somem ao reiniciar.
- **Shell**: edição de linha com setas, Home, End e Delete; histórico de 16 linhas; aspas; variáveis com `set` e `$NOME`; scripts com `sh ARQUIVO`.
- **Comandos de arquivos**: `ls [-l]`, `cd`, `pwd`, `mkdir`, `touch`, `cat`, `write`, `append`, `rm`, `rmdir`, `cp`, `mv`, `tree`.
- **Editor `edit`** (ou `vi`): modos normal, inserção e comando; `:w`, `:q`, `:wq`, `:q!`; `:int` compila o buffer para `.int`; `:run` compila e executa.
- **Gerenciador `files`**: navega em pastas, cria arquivos e pastas, renomeia, apaga com confirmação, copia e move.
- **Linguagens**: `python` (arquivo ou `-c`) compila e executa Python; `pyc` gera `.int`; `int` executa; `intinfo` verifica o cabeçalho.
- **Pacotes**: `pkg install`, `remove`, `list`, `info` e `update`, com formato `.bkg` próprio e registro em `/var/lib/pkg`. Conflitos com arquivos do usuário são recusados antes de qualquer mudança.

Guias completos: [docs/SHELL.md](docs/SHELL.md), [docs/PYTHON_SUBSET.md](docs/PYTHON_SUBSET.md), [docs/INT_FORMAT.md](docs/INT_FORMAT.md) e [docs/PACKAGES.md](docs/PACKAGES.md).

## O que a 0.3.1 **não** faz

- Não há interface gráfica. Ela é trabalho para a comunidade, sobre a base de texto.
- Não há rede: nenhum driver de placa de rede. Atualizações de pacotes leem um diretório local (`/repo`), e não baixam nada.
- Não há armazenamento persistente. Arquivos, pacotes e o registro somem ao reiniciar (Fase 8).
- Não há `if`, `for` nem `while` na linguagem de shell (use Python para lógica).
- Não há `def`, `class`, listas, dicionários, `None` nem divisão real `/` no Python. Veja [docs/PYTHON_SUBSET.md](docs/PYTHON_SUBSET.md).
- C, C++ e Assembly ainda não executam dentro do sistema. Só o caminho Python -> `.int` -> máquina virtual existe nesta versão.
- Não há processos, escalonador, paginação nem isolamento de privilégios (Fases 9). O kernel roda em ring 0.
- Não há suporte a USB, áudio, GPU, NVMe ou mouse.
- O editor não tem desfazer (undo).

## Portabilidade: 8, 16, 32 e 64 bits

Pergunta frequente: "o sistema roda em qualquer PC?". Resposta honesta:

- **32 bits (i686)**: é a arquitetura do kernel hoje. Roda em PCs x86 de 32 bits.
- **64 bits**: PCs de 64 bits rodam o kernel de 32 bits em modo de compatibilidade, pelo GRUB (`i386-pc`). Isso não foi testado em hardware real; só no QEMU.
- **16 bits e 8 bits**: não são suportados. O kernel depende de modo protegido de 32 bits, de interrupções e de paginação de 32 bits. Rodar em um 8086, 286 ou processador de 8 bits exige reescrever o kernel, não só recompilar.
- **Outras arquiteturas** (ARM, RISC-V e outras): também exigem um porte completo: boot, interrupções, teclado, VGA, serial e gerência de memória.
- **Portável já hoje**: a máquina virtual `.int` e o compilador Python são C puro, sem assembly nem dependência do kernel. Foram testados no computador de desenvolvimento (120 verificações de compilador, VM e verificador). Um porte futuro reaproveita essas partes.

## Compilar e executar

Veja [BUILDING.md](BUILDING.md) para instalar as ferramentas. No diretório do projeto:

```sh
make            # compila o kernel: build/kernel.elf
make iso        # gera a ISO inicializável: build/backdoors.iso
make run        # inicializa a ISO no QEMU (janela + serial no terminal)
make test       # todos os testes (unitários, boot, shell e aplicativos)
make clean      # remove build/
```

Outros alvos: `make debug` (QEMU esperando o GDB na porta 1234) e `make test-panic` (build com falha fatal provocada).

## Testes

`make test` executa quatro conjuntos:

1. **Unitários do host** (`tests/unit/`): parser de linha do shell (7 casos) e compilador, VM e verificador `.int` (120 verificações).
2. **Boot** (`tests/boot/test_boot.sh`): quatro casos no QEMU, lendo serial e VGA: boot normal (GDT, IDT, timer, memória, heap, initramfs e shell), falha fatal provocada, `int3` que volta ao código e divisão por zero que vira pânico. A ISO passa pelo GRUB, então o cabeçalho Multiboot é validado de verdade.
3. **Shell** (`tests/boot/shell_test.py`): digita comandos pelo monitor do QEMU (`sendkey`), verifica saída, histórico, backspace, `clear` e `reboot`.
4. **Aplicativos** (`tests/boot/apps_test.py`): uma sessão exercita arquivos, Python, `.int`, scripts, pacotes (com conflito e atualização), o editor (`:w`, `:int`, `:run`, erro de sintaxe, E37) e o gerenciador de arquivos pela tela VGA.

## Estrutura

```
boot/multiboot_header.asm   cabeçalho Multiboot 1 (GRUB)
entry/entry.asm             _start: zera .bss, pilha, chama kernel_main
entry/linker.ld             kernel carregado em 1 MiB
kernel/core/                kernel.c, gdt.c, idt.c, interrupts.asm, panic.c, log.c, cpu.c, string.c
kernel/drivers/             serial, vga_text, pic, timer, keyboard, rtc
kernel/mm/                  pmm.c (frames físicos), heap.c (kmalloc/kfree)
kernel/fs/                  vfs.c (árvore em memória e leitor cpio)
kernel/lang/                py_compile.c (Python -> .int), int_vm.c, int_image.c (verificador)
kernel/pkg/                 pkg.c (instalar, remover, listar, atualizar pacotes .bkg)
kernel/shell/               shell.c, parser.c, history.c, script.c, commands.c, cmd_*.c
kernel/apps/                ui.c, editor.c, files.c
include/                    cabeçalhos públicos (arch/, kernel/, drivers/, fs/, lang/, pkg/, shell/, apps/)
initramfs/                  arquivos embutidos na ISO (home/, LEIA-ME.txt)
packages/hello/             pacote de exemplo (fonte)
tools/                      mkinitramfs.py, mkpkg.py (geram o cpio e os .bkg)
iso/boot/grub/grub.cfg      menu do GRUB da ISO
docs/                       guias de shell, Python, formato .int e pacotes
tests/                      unit/, boot/ (test_boot.sh, check_output.py, shell_test.py, apps_test.py, qemu_session.py)
```

Documentação: [BUILDING.md](BUILDING.md) (compilação), [ARCHITECTURE.md](ARCHITECTURE.md) (arquitetura e decisões) e [docs/](docs/) (guias de uso).

## Limitações conhecidas

- Testado apenas no Ubuntu 24.04 (GCC 13.3, NASM 2.16, binutils 2.42, QEMU 8.2.2) com `qemu-system-i386`.
- Nenhum teste em hardware real. Teclado e RTC foram testados só no QEMU.
- Compilação para i686 com `gcc -m32` do sistema, e não com `i686-elf-gcc` (veja [ARCHITECTURE.md](ARCHITECTURE.md)).
- Caminhos Windows (MSYS2) não foram testados. O caminho validado é WSL2.
- Mensagens do kernel em inglês e ASCII (a VGA usa a codepage 437). Os textos do shell e dos apps estão em português ASCII, sem acentos, pelo mesmo motivo.
- `CONTRIBUTING.md` e `SECURITY.md` ainda não existem.
