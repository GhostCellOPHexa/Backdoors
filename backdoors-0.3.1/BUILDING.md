# Compilando o Backdoors OS

Este documento descreve como instalar as ferramentas e compilar, testar e executar o sistema.

## Ferramentas necessárias

| Ferramenta | Para quê | Variável no Makefile |
|------------|----------|----------------------|
| `gcc` com suporte a `-m32` | compilar o kernel em C (freestanding, i686) | `CC` |
| `nasm` | Assembly do boot e do entry | `AS` |
| `ld` (GNU binutils) | linkar o kernel ELF i386 | `LD` |
| `grub-mkrescue` + `xorriso` | gerar a ISO com GRUB | `GRUB_MKRESCUE` |
| `grub-file` | validar o cabeçalho Multiboot | `GRUB_FILE` |
| `qemu-system-i386` | executar e testar a ISO | `QEMU` |
| `make` | orquestrar a build | — |
| `python3` | gera o initramfs e os pacotes `.bkg` (`tools/`), e roda os testes de boot, shell e aplicativos | `PYTHON` |

Verifique as ferramentas com:

```sh
make check-tools
```

Se algo estiver faltando, o comando indica qual ferramenta falta.

## Linux (Ubuntu / Debian)

Testado no Ubuntu 24.04. Os comandos abaixo instalam tudo que o projeto usa:

```sh
sudo apt-get update
sudo apt-get install -y build-essential nasm binutils \
    grub-pc-bin grub-common xorriso mtools \
    qemu-system-x86 python3
```

Notas:

- `gcc -m32 -ffreestanding -c` funcionou com o GCC do sistema **sem** o pacote `gcc-multilib`. A compilação do kernel não usa libc nem cabeçalhos de 32 bits do sistema.
- O `grub-pc-bin` fornece os módulos `i386-pc` que o `grub-mkrescue` usa para gerar uma ISO inicializável por BIOS.

Outras distribuições: os nomes dos pacotes mudam, mas são os mesmos componentes (gcc, nasm, binutils, grub-pc-bin/grub-common, xorriso, qemu-system-x86).

## Windows

Recomendação: use **WSL2** com Ubuntu e siga a seção Linux dentro dele.

```powershell
wsl --install -d Ubuntu
```

Depois, dentro do Ubuntu, use os comandos da seção Linux.

**Atenção:** o GCC do MSYS2 não foi validado para este projeto. Ele pode não ter suporte freestanding i686 completo nem o `grub-mkrescue` com os módulos `i386-pc`. O caminho validado é WSL2. Se usar MSYS2, rode `make check-tools` para confirmar cada ferramenta antes de compilar.

## Compilar

```sh
make            # ou: make all
```

Saída: `build/kernel.elf`. O build usa `-Wall -Wextra -Werror`: qualquer aviso interrompe a compilação.

## Gerar a ISO

```sh
make iso
```

Saída: `build/backdoors.iso`. Antes de gerar, `grub-file --is-x86-multiboot` valida o kernel.

## Executar no QEMU

```sh
make run
```

Abre o QEMU com a ISO, mostra o console VGA e envia a serial para o terminal.

Para depurar com o GDB:

```sh
make debug                  # QEMU para na primeira instrução e espera o GDB
gdb build/kernel.elf
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

## Testes

```sh
make test
```

`make test` executa, em ordem: os testes unitários do host (`tests/unit/run_unit.sh`), os casos de boot (`tests/boot/test_boot.sh`), o teste do shell (`tests/boot/shell_test.py`) e o teste dos aplicativos (`tests/boot/apps_test.py`). Os três últimos rodam no QEMU. Os logs ficam em `build/test/`. Cada verificação é impressa com o resultado.

`make test-panic` gera uma ISO com a falha fatal provocada, para inspeção manual.

## Outros alvos

| Alvo | Função |
|------|--------|
| `make clean` | remove `build/` |
| `make check-tools` | verifica as ferramentas necessárias |
| `make test-panic` | build separado com `BD_TEST_PANIC` (falha fatal provocada) |

## Variáveis úteis

```sh
make CC=i686-elf-gcc        # usar um cross-compiler, se você tiver um
make QEMU=qemu-system-i386  # caminho alternativo do QEMU
```

Com `i686-elf-gcc` as flags de `CFLAGS` podem precisar de ajuste (por exemplo, `-m32` pode não ser aceita). Essa combinação não foi testada neste repositório.

## Solução de problemas

- **`gcc -m32 ... não funciona`**: instale `gcc-multilib` (Ubuntu/Debian) ou use WSL2.
- **`grub-mkrescue` falha**: o log completo é mostrado na saída. Confirme que `xorriso` e `grub-pc-bin` estão instalados.
- **`make test` reporta falha de serial**: verifique se o QEMU foi iniciado com `-serial`, e se o log `build/test/boot/serial.log` contém as linhas esperadas.
