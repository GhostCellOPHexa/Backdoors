# Arquitetura do Backdoors OS (versão 0.3.1)

Este documento descreve o que existe hoje (Fases 1 a 3). Partes futuras estão marcadas como tal.

## Visão geral

O Backdoors OS é um kernel monolítico pequeno para i686 (x86 de 32 bits). Até a versão 0.2.0 o kernel inicializa GDT, IDT, PIC, timer e teclado, e roda um shell de texto.

```
GRUB (Multiboot)
   │  EAX = 0x2BADB002, EBX = info Multiboot, modo protegido 32 bits
   ▼
_start  (entry/entry.asm)
   │  zera .bss, configura pilha de 16 KiB, chama kernel_main(magic, info)
   ▼
kernel_main  (kernel/core/kernel.c)
   ├─ vga_init()            console VGA 80x25 em 0xB8000
   ├─ serial_init()         COM1 115200 8N1, com teste de loopback
   ├─ verifica magic        se inválido → kpanic()
   ├─ gdt_init()            GDT plana, recarrega CS e seletores
   ├─ idt_init()            48 vetores (exceções 0..31, IRQs 32..47)
   ├─ pic_init()            remapeia IRQs para 32..47, mascara tudo
   ├─ timer_init(100)       PIT canal 0 a 100 Hz (IRQ0)
   ├─ cpu_enable_interrupts()
   ├─ keyboard_init()       IRQ1 (PS/2)
   ├─ kprintf(...)          banner na tela e na serial
   └─ shell_run()           laço de comandos (não retorna)
```

## Boot

- **Cabeçalho Multiboot** (`boot/multiboot_header.asm`): magic `0x1BADB002`, flags `0`, checksum calculado pelo NASM. Fica na primeira seção do `.text`, dentro dos primeiros 8 KiB do arquivo, como exige a especificação.
- **Entrada** (`entry/entry.asm`): desativa interrupções (`cli`), define direção de cópia (`cld`), guarda o magic em `ESI` antes de zerar `.bss` (a instrução `rep stosb` altera `EAX`), e prepara a pilha. Antes do `call`, `ESP` está alinhado a 16 bytes, como exige a ABI System V i386. Os argumentos `(magic, info)` seguem a ordem cdecl.
- **Pilha**: 16 KiB em `.bss`, alinhada a 16 bytes. Não há proteção de guarda: estouro de pilha não é detectado nesta versão.

## Layout de memória

O kernel é carregado em `1 MiB` físico (`0x100000`), como o GRUB espera para kernels ELF. O `entry/linker.ld` organiza:

| Seção | Início | Conteúdo |
|-------|--------|----------|
| `.text` | `0x100000` | cabeçalho Multiboot primeiro, depois código |
| `.rodata` | alinhado a 4 KiB após `.text` | constantes |
| `.data` | alinhado a 4 KiB após `.rodata` | variáveis inicializadas |
| `.bss` | alinhado a 4 KiB após `.data` | `__bss_start`..`__bss_end` (zerado no boot), pilha |

Símbolos de linker expostos: `__bss_start`, `__bss_end`, `__kernel_end`.

**Limites desta versão:** o kernel não sabe quanta memória física existe. O Multiboot pode fornecer o mapa de memória, mas o cabeçalho atual não pede essas informações (`flags = 0`); isso será tratado na Fase 4. Não há paginação, então todos os endereços são físicos.

## Drivers

### Serial COM1 (`kernel/drivers/serial.c`)

- Porta base `0x3F8`, 115200 bps, 8 bits, sem paridade, 1 stop bit, FIFO habilitado.
- `serial_init()` testa a porta em modo loopback (envia `0xAE` e espera o mesmo byte de volta). Se o teste falhar, `serial_init()` retorna `false` e as escritas viram no-op.
- `serial_write_char()` converte `\n` em `\r\n` e espera o registrador de transmissão esvaziar, com limite de tentativas, para não travar sem UART. Se o limite estourar, o caractere é descartado.

### VGA texto (`kernel/drivers/vga_text.c`)

- Memória em `0xB8000`, 80 colunas por 25 linhas, células de 2 bytes (caractere + atributo).
- Rolagem para cima quando a última linha é preenchida.
- Cursor de hardware movido pelas portas `0x3D4`/`0x3D5`.
- Suporta `\n` e `\r`. Não há tabulação, backspace nem cores além do atributo bruto.

## Saída do kernel

- `kprintf()` (`kernel/core/log.c`): escreve na VGA **e** na serial.
- `klog()`: escreve somente na serial, para diagnósticos.
- Conversões: `%s %c %d %u %x %X %%`. Verificadas em tempo de compilação pelo `__attribute__((format(printf)))`.
- Mensagens em ASCII: a VGA usa a codepage 437, onde bytes UTF-8 aparecem errados.

## Falha fatal (`kernel/core/panic.c`)

`kpanic(msg)`:

1. desativa interrupções;
2. evita recursão (flag `in_panic`);
3. muda o atributo da VGA para branco sobre vermelho;
4. imprime `KERNEL PANIC: <msg>` e `System halted.` na tela e na serial;
5. para a CPU com `cli; hlt`.

## Memória de runtime

`kernel/core/string.c` implementa `memset`, `memcpy` e `memmove`. Não são chamadas diretamente pela versão 0.1.0, mas um ambiente freestanding precisa tê-las, porque o GCC pode gerar chamadas a elas. O build usa `-fno-tree-loop-distribute-patterns` para que essas próprias funções não sejam substituídas por chamadas recursivas.

## Decisões de build

- **`gcc -m32` em vez de `i686-elf-gcc`.** O requisito pede um cross-compiler, ou uma alternativa justificada. Usamos o GCC do sistema com `-m32 -ffreestanding`, porque: (1) a busca no apt do Ubuntu 24.04 não encontrou pacote `i686-elf`; (2) o código não depende de libc nem de bibliotecas do hospedeiro (`-nostdlib`, link manual com `ld -m elf_i386`); (3) `-m32` gera código i686 e os headers freestanding do GCC (`stdint.h`, `stddef.h`, `stdarg.h`, `stdbool.h`) não dependem da libc. Um cross-compiler continua suportado via `make CC=...`, mas não foi testado.
- **Multiboot com GRUB** em vez de bootloader próprio. Simplifica a geração da ISO e a inicialização em modo protegido de 32 bits. Não há código de boot próprio misturado ao GRUB.
- **`-Werror`** e **`-fno-stack-protector -fno-pic -fno-pie`**: sem proteção de pilha nem código independente de posição, porque o kernel roda sem o runtime que essas proteções exigem.
- **Compilação e link** via Makefile, sem CMake, para manter o build simples e reproduzível.

## Testes de boot

`tests/boot/test_boot.sh` executa o QEMU sem janela e:

1. lê a COM1 pelo arquivo de log (`-serial file:...`);
2. despeja a memória VGA com o monitor HMP (`xp /4000xb 0xb8000`), depois de 5 segundos;
3. verifica os dois registros com `tests/boot/check_output.py`.

O caso de pânico usa uma build separada (`make test-panic`, com `-DBD_TEST_PANIC`) que chama `kpanic` logo após a inicialização da serial. Esse hook existe só em build de teste.

## O que não existe ainda (não implementado)

- Armazenamento persistente: disco, FAT ou outro sistema de arquivos em disco (Fase 8).
- Escalonador, processos, chamadas de sistema, separação de privilégios (Fase 9).
- Paginação e memória virtual (Fase 9).
- Rede: não há driver de placa de rede, então pacotes não são baixados.
- Execução de C, C++ e Assembly dentro do sistema (só Python, via `.int`).
- Interface gráfica (fora do escopo; trabalho para a comunidade).

## Descritores, interrupções e temporização (Fase 2)

- **GDT** (`kernel/core/gdt.c`): 5 entradas (nulo, código e dados do kernel, código e dados de usuário). Todas planas (base 0, limite 4 GiB), granularidade de 4 KiB. Os seletores são 0x08, 0x10, 0x1B e 0x23 (estes com RPL 3). `gdt_flush` (NASM) faz `lgdt`, recarrega DS/ES/FS/GS/SS e faz um far jump para CS. A TSS ainda não existe: ela entra na Fase 9.
- **IDT** (`kernel/core/idt.c`, `interrupts.asm`): gates de interrupção (`0x8E`) para 48 vetores. Os stubs empilham um quadro uniforme (`int_no`, `err_code`) e chamam `isr_dispatch(struct regs *)`. Exceções com código de erro (8, 10 a 14, 17) não ganham zero extra.
- **Exceções:** `#BP` (3) é registrada na serial e o código volta. As demais são fatais: mostram vetor, nome, código de erro, EIP, CS e EFLAGS (e CR2 em falha de página) e chamam `kpanic`.
- **PIC 8259** (`kernel/drivers/pic.c`): remapeado para os vetores 32..47. Todos os IRQs começam mascarados; `pic_unmask` libera cada um. O EOI é enviado pelo `isr_dispatch`.
- **PIT** (`kernel/drivers/timer.c`): canal 0, modo 3, divisor 1193182/hz. Os ticks são contados em IRQ0. `timer_wait_ticks` usa `sti; hlt` e compara com aritmética de sinal, sem problemas de overflow.

## Teclado e shell (Fase 3)

- **Teclado** (`kernel/drivers/keyboard.c`): IRQ1 lê a porta 0x60 e traduz o scancode set 1. Trata Shift, CapsLock, setas (prefixo 0xE0) e ignora liberações de tecla. Fila circular de 64 teclas. `keyboard_getkey` desabilita interrupções só ao retirar da fila, então o IRQ nunca espera.
- **Shell** (`kernel/shell/shell.c`): `readline` edita a linha (backspace) e navega no histórico (`KEY_UP`/`KEY_DOWN`). Cada comando é despachado por uma tabela (`commands.c`). Os não implementados guardam a fase que os desbloqueia. O prompt (`shell_set_prompt`) é verde no VGA.
- **Parser** (`kernel/shell/parser.c`): divide por espaços e agrupa com aspas duplas. Erros de sintaxe (aspas sem fechamento, argumentos demais) são informados. A string é alterada no lugar, e o separador é consumido antes de gravar o terminador.
- **Histórico** (`kernel/shell/history.c`): 16 linhas em anel. Não repete a última linha e ignora linhas vazias.
- **RTC** (`kernel/drivers/rtc.c`): lê o CMOS (portas 0x70/0x71). Espera o fim de atualizações e lê até duas leituras iguais. Converte BCD e a hora de 12 horas se necessário.
- **CPUID** (`kernel/core/cpuid.asm`): primeiro verifica se o bit ID do EFLAGS pode ser alterado, depois executa a instrução.
- **Reboot:** pulso de reset pelo controlador de teclado (porta 0x64, comando 0xFE).

## Decisões da Fase 2 e 3

- **Teclado e fila em interrupção:** a fila é um buffer circular de tamanho fixo. A IRQ só escreve nela, e o shell só lê com interrupções desabilitadas durante a retirada.
- **Sem TSS e sem ring 3 ainda:** o kernel roda inteiro em ring 0. O seletor de usuário existe na GDT para a Fase 9, mas nenhum código o usa hoje.
- **Mensagens ASCII:** VGA com codepage 437 mostra bytes UTF-8 como lixo. Por isso as strings impressas pelo kernel e pelo shell não têm acentos.

## Memória física e heap (Fase 4)

- **Mapa do bootloader** (`include/kernel/multiboot.h`): o kernel pede memória básica (bit1) e aceita o mapa de memória que o GRUB devolve (flag de informação bit6, lido de `mmap_addr`). Só entradas do tipo `AVAILABLE` viram frames livres. Sem mapa, usa `mem_upper` acima de 1 MiB e registra um aviso. Sem nenhuma informação, o kernel entra em pânico.
- **Cabeçalho Multiboot:** `MB_FLAGS` é `0x3` (alinhar módulos e memória básica). Um primeiro teste usou `0x43`, que o QEMU com `-kernel` aceita mas o GRUB rejeita com "unsupported flag". O bit6 é um flag de *informação* devolvido pelo bootloader, não um pedido no cabeçalho. Por isso a ISO é testada pelo GRUB, e não só com `-kernel`.
- **PMM** (`kernel/mm/pmm.c`): bitmap estático de 2^20 frames (4 GiB, 128 KiB em `.bss`). Começa tudo ocupado e libera o que o mapa declara disponível. Reserva [0, 1 MiB), a imagem do kernel (`__kernel_start`..`__kernel_end`), a estrutura Multiboot, o próprio mapa e os módulos. `pmm_alloc_frame` usa um cursor de busca; `pmm_alloc_contig` procura uma faixa contígua, usada pelo heap. `pmm_free_frame` registra erros de free inválido ou não alocado.
- **Heap** (`kernel/mm/heap.c`): 4 MiB contíguos obtidos do PMM. Cada bloco tem cabeçalho de 32 bytes alinhado a 16, com magic (`LIVE`/`FREE`) e ligações `next`/`prev` na região. Primeiro ajuste; divide o bloco quando sobra espaço para outro cabeçalho mais 16 bytes; funde com o vizinho seguinte e o anterior ao liberar. `kfree` detecta liberação dupla, ponteiro inválido e corrupção, registra no log e conta em `errors`.
- **Limites:** o heap usa endereços físicos (identidade), sem paginação. Não há reserva de memória para DMA e não há proteção de páginas. Tudo isso fica para a Fase 9.
- **Teste de boot:** `heap_selftest` cria três blocos, libera o do meio, aloca um menor no mesmo lugar e confirma que, liberando tudo, sobra um único bloco livre. Falha de self-test é pânico.

## Sistema de arquivos, linguagem e pacotes (Fases 5 a 7, versão 0.3.1)

**VFS em memória** (`kernel/fs/vfs.c`, `include/fs/vfs.h`). A árvore inteira fica no heap do kernel: nós de diretório e de arquivo, com nomes de até 31 caracteres. O initramfs é um arquivo cpio `newc` carregado pelo GRUB como módulo (`/boot/initramfs.cpio`); `vfs_load_cpio` cria os diretórios e arquivos a partir da raiz, e `vfs_cpio_walk` percorre o mesmo formato para os pacotes. Não há disco: tudo some ao reiniciar até a Fase 8. Os caminhos são absolutos ou relativos ao diretório atual, e `vfs_strerror` dá as mensagens curtas que o shell exibe.

**Formato `.int`** (`include/lang/int.h`, `kernel/lang/int_image.c`). Cabeçalho de 32 bytes com magic `INT1`, versão, tamanhos de código e constantes, número de variáveis e checksum FNV-1a. A imagem é verificada inteira antes de executar (tamanhos, checksum, opcodes, operandos, saltos sobre um mapa de limites de instrução, slots, constantes e a última instrução `HALT`). A verificação antes da execução impede que um arquivo corrompido leia memória fora do programa, mas não é uma sandbox contra imagens feitas de propósito, porque ainda não há isolamento de processos.

**Máquina virtual** (`kernel/lang/int_vm.c`). Máquina de pilha, com 128 valores na pilha, 64 variáveis, uma área de 4 KiB para textos em tempo de execução e valores `None`, booleano, inteiro de 32 bits ou texto. Inteiros dão a volta em 32 bits; `//` e `%` seguem o Python. A VM não usa nada do kernel além de uma função de saída e uma função de parada: por isso o código é C portável, testado no host com 120 verificações (`tests/unit/int_test.c`). É essa parte que um porte futuro reaproveita.

**Compilador Python** (`kernel/lang/py_compile.c`). Uma passagem: analisador léxico com indentação (INDENT/DEDENT) e junção de linhas dentro de parênteses; analisador descendente que emite bytecode e faz remendo de saltos (`patch32`). `for NAME in range(...)` vira um laço com contador e fim em slots escondidos. Ao final, o compilador grava o cabeçalho, copia código e constantes, calcula o checksum e verifica a própria saída. Erros trazem o número da linha. O subconjunto está em [docs/PYTHON_SUBSET.md](docs/PYTHON_SUBSET.md).

**Shell de scripts** (`kernel/shell/script.c`). Variáveis em uma tabela de 16 entradas: `set` grava, `$NOME` é trocado antes do parser, `sh` executa um arquivo linha a linha com profundidade máxima de 4. Não há controle de fluxo; a linguagem de shell é deliberadamente simples.

**Pacotes** (`kernel/pkg/pkg.c`, `tools/mkpkg.py`). Um `.bkg` é um cpio `newc` com `PKG/INFO` como primeira entrada. A instalação lê e valida o pacote inteiro antes de mudar qualquer coisa; conflitos com arquivos que não pertencem ao pacote são recusados antes da extração. O registro em `/var/lib/pkg/NOME` lista os arquivos instalados, e é ele que sustenta `pkg remove` e a troca de versão. `pkg update` lê um diretório de repositório (padrão `/repo`), sem rede.

**Editor e gerenciador** (`kernel/apps/`). O editor mantém o texto em uma matriz de até 512 linhas por 119 colunas, com modos normal, inserção e comando, e desenha direto na VGA. O gerenciador usa a mesma biblioteca de interface (`ui.c`: texto em posição, prompts e espera de tecla). Ambos leem teclas pela fila do teclado, então funcionam com as setas e com a interrupção por ESC.

**Testes desta versão.** `apps_test.py` roda uma sessão inteira no QEMU, digitando comandos pelo monitor e lendo a tela da VGA com `xp /4000xb 0xb8000`. O parser dos dumps fica em `tests/boot/qemu_session.py`, compartilhado com o teste do shell.
