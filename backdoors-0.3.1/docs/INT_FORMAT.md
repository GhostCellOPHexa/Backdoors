# Formato `.int` e máquina virtual (versão 1)

Um arquivo `.int` é a imagem de um programa para a máquina virtual do
Backdoors OS. Ele é gerado pelo compilador do subconjunto de Python
([PYTHON_SUBSET.md](PYTHON_SUBSET.md)) e pode ser executado com `int` e
inspecionado com `intinfo`.

A máquina virtual é escrita em C portável (sem assembly), então a mesma
imagem roda em qualquer arquitetura onde o kernel for portado. Essa parte é
testada no computador de desenvolvimento (`make test`), não no hardware
alvo.

## Layout do arquivo

| offset | tamanho | campo |
|-------:|--------:|-------|
| 0 | 4 | magic `INT1` |
| 4 | 2 | versão do formato (1) |
| 6 | 2 | tamanho do cabeçalho (32) |
| 8 | 4 | tamanho do código, em bytes |
| 12 | 4 | tamanho da tabela de constantes, em bytes |
| 16 | 4 | número de variáveis (slots, até 64) |
| 20 | 4 | checksum FNV-1a de código e constantes |
| 24 | 8 | reservado (zero) |
| 32 | n | código (bytecode) |
| 32+n | m | constantes: bytes dos textos, sem terminador |

Inteiros são little-endian. Limites: código até 32 KiB, constantes até
4 KiB, 64 variáveis.

## Verificação antes de executar

Toda imagem é verificada antes de rodar, tanto em `int` quanto em `intinfo`:

- magic, versão e tamanho do cabeçalho;
- tamanhos de código e constantes dentro dos limites e do arquivo;
- checksum FNV-1a conferindo com código e constantes;
- cada opcode conhecido, com operandos completos;
- saltos apontando para o início de uma instrução (um mapa de limites de
  instruções garante isso);
- slots de variáveis e intervalos de constantes dentro do permitido;
- a última instrução é `HALT`.

Uma imagem que falha qualquer item é recusada com mensagem, sem executar
nada. Isso protege contra arquivos corrompidos, mas **não** é uma sandbox
contra imagens maliciosas construídas de propósito; o sistema ainda não tem
isolamento de processos (Fase 9).

## Conjunto de instruções

Operandos em parênteses, em bytes, little-endian.

| Opcode | Operandos | Efeito |
|--------|-----------|--------|
| `HALT` | — | termina o programa |
| `PUSH_INT` | i32 | empilha inteiro |
| `PUSH_STR` | u32 deslocamento, u16 tamanho | empilha texto da tabela de constantes |
| `LOAD` | u8 slot | empilha a variável |
| `STORE` | u8 slot | desempilha para a variável |
| `POP` | — | descarta o topo |
| `ADD`, `SUB`, `MUL` | — | aritmética; `ADD` entre textos concatena |
| `FLOORDIV`, `MOD` | — | divisão e resto com semântica do Python |
| `NEG`, `NOT` | — | unário |
| `EQ`, `NE`, `LT`, `LE`, `GT`, `GE` | — | comparações |
| `JMP` | u32 alvo | salto incondicional |
| `JF` | u32 alvo | desempilha; salta se falso |
| `JT_KEEP`, `JF_KEEP` | u32 alvo | usados por `and` e `or` (mantêm o topo ao saltar) |
| `CALL` | u8 função, u8 argc | chama função embutida |

Funções embutidas: `0` = `print`, `1` = `len`, `2` = `str`, `3` = `int`.

## Semântica dos valores

- Valores são `None`, booleano, inteiro de 32 bits ou texto.
- Aritmética de inteiros dá a volta em 32 bits (complemento de dois).
- `FLOORDIV` e `MOD` seguem o Python: o quociente é arredondado para baixo
  e o resto tem o sinal do divisor. Divisão por zero é erro de execução.
- Textos em tempo de execução vivem numa área de 4 KiB; passar disso é erro
  ("memoria de strings esgotada"), não corrupção.
- A pilha tem 128 valores. Estouro é erro de execução.

## Execução e interrupção

`int_run` recebe uma função de saída (usada por `print`) e uma função
`should_stop`, consultada a cada 1024 passos. A shell passa uma função que
retorna verdadeiro quando ESC é pressionado. Assim, um laço infinito pode
ser interrompido sem reiniciar a máquina.

## Ferramentas

- `pyc ARQUIVO.py [SAIDA.int]` compila e grava o `.int` (o nome padrão troca
  `.py` por `.int`). O compilador verifica a própria saída antes de gravar.
- `intinfo ARQUIVO.int` verifica e mostra versão, tamanhos, número de
  variáveis, instruções e checksum.
- `int ARQUIVO.int` verifica e executa.
- No editor: `:int` compila o buffer, `:run` compila e executa.

## Por que não há C, C++ ou Assembly ainda

A Fase 6 entrega só o caminho Python -> `.int` -> máquina virtual. Executar
C, C++ e Assembly dentro do sistema depende de um toolchain ou de um
montador rodando no próprio sistema, o que não existe nesta versão.
