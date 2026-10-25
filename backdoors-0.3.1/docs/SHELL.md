# Shell, scripts, editor e gerenciador de arquivos

Tudo é texto. Não há mouse nem interface gráfica. Teclas: layout americano (US).

## Comandos

Digite `help` para a lista atual. Resumo por grupo:

| Grupo | Comandos |
|-------|----------|
| Sistema | `help`, `clear`, `version`, `about`, `echo`, `date`, `uptime`, `mem`, `cpu`, `devices`, `reboot`, `panic-test` (desativado por padrão) |
| Variáveis e scripts | `set NOME VALOR`, `sh ARQUIVO` |
| Arquivos | `ls [-l] [CAMINHO]`, `cd`, `pwd`, `mkdir`, `touch`, `cat`, `write`, `append`, `rm`, `rmdir`, `cp`, `mv`, `tree` |
| Aplicativos | `edit` (ou `vi`), `files` |
| Linguagens | `python`, `pyc`, `int`, `intinfo` (veja [PYTHON_SUBSET.md](PYTHON_SUBSET.md) e [INT_FORMAT.md](INT_FORMAT.md)) |
| Pacotes | `pkg install`, `pkg remove`, `pkg list`, `pkg info`, `pkg update` (veja [PACKAGES.md](PACKAGES.md)) |

Ainda indisponíveis, com a fase que falta: `mount`, `umount` (Fase 8),
`tasks`, `ps`, `kill` (Fase 9).

Regras gerais:

- Aspas duplas agrupam palavras com espaços: `echo "ola  mundo"` imprime
  `ola  mundo`.
- Caminhos são absolutos (`/home/ola.py`) ou relativos ao diretório atual.
- `rm` apaga só arquivos. Diretórios usam `rmdir` (só se estiverem vazios).
- `cp` copia só arquivos. `mv` move ou renomeia; se o destino for um
  diretório, o item entra dentro dele.
- `write` sobrescreve o arquivo; `append` acrescenta ao fim.
- Setas para cima e para baixo percorrem o histórico de 16 linhas. Setas
  esquerda e direita, Home, End e Delete editam a linha no meio do texto.
- Use a seta para cima para repetir um comando longo.

## Variáveis e scripts

```
set nome Backdoors
echo Ola, $nome!
```

- O nome tem letras, dígitos e `_`, até 15 caracteres. Há no máximo 16
  variáveis ao mesmo tempo.
- Para um valor com espaços, use aspas: `set titulo "Meu sistema"`.
- `$NOME` é trocado pelo valor antes de a linha ser executada.

Um script é um arquivo de texto com um comando por linha:

```
# Comentário: linhas que começam com # são ignoradas.
set nome Backdoors
echo Ola, $nome!
cd /home
ls
```

`sh /home/demo.sh` executa o arquivo. Linhas em branco e comentários são
pulados. Um script pode chamar outro com `sh`, até 4 níveis de profundidade.

Ainda não há `if`, `for` nem `while` na linguagem de shell. Para lógica,
use um programa Python.

## Editor (`edit ARQUIVO`, ou `vi`)

Modos: NORMAL (padrão), INSERIR e COMANDO. A linha de status mostra o modo,
o nome do arquivo, `[+]` se houver alterações não gravadas, e a posição.

**Modo NORMAL**

| Tecla | Ação |
|-------|------|
| `h` `j` `k` `l` ou setas | move o cursor |
| `0` `$` ou Home End | início e fim da linha |
| `w` `b` | próxima e anterior palavra |
| `gg` `G` | início e fim do arquivo |
| PgUp PgDn | rola uma tela |
| `i` `a` `I` `A` | entra no modo inserção (no cursor, depois, início e fim da linha) |
| `o` `O` | abre linha abaixo e acima, e entra no modo inserção |
| `x` | apaga o caractere do cursor |
| `dd` | apaga a linha |
| `yy` | copia a linha |
| `p` `P` | cola abaixo e acima |
| `:` | digita um comando |

**Modo INSERIR**

| Tecla | Ação |
|-------|------|
| texto | insere no cursor |
| Enter | divide a linha |
| Backspace | apaga antes do cursor; no início da linha, junta com a anterior |
| Delete | apaga o caractere do cursor |
| Tab | insere 4 espaços |
| ESC | volta ao modo NORMAL |

**Comandos** (digitados depois de `:`, e Enter):

| Comando | Ação |
|---------|------|
| `:w` `:w NOME` | grava (o segundo grava com outro nome) |
| `:q` | sai; recusa se houver alterações não gravadas (E37) |
| `:q!` | sai descartando alterações |
| `:wq` `:x` | grava e sai |
| `:int` | compila o buffer para `.int` no mesmo nome, com `.py` trocado por `.int`. Se houver erro, o cursor vai para a linha |
| `:run` | compila e executa; ESC interrompe; no fim, pressione uma tecla para voltar |
| `:help` | resumo dos comandos |

Limites: 512 linhas, 119 caracteres por linha. Linhas longas são cortadas e
o editor avisa. Não há desfazer (undo) nesta versão.

## Gerenciador de arquivos (`files`)

Mostra o diretório atual. Na primeira linha, o caminho. Pastas aparecem
antes dos arquivos e com `/` no fim. Quando não está na raiz, a primeira
entrada é `../`.

| Tecla | Ação |
|-------|------|
| setas, `j` `k` | move a seleção |
| PgUp PgDn, Home End | rola e vai ao início ou ao fim |
| Enter | abre a pasta, ou edita o arquivo no editor |
| Backspace ou seta esquerda | sobe um nível |
| `e` | edita o arquivo selecionado |
| `n` | cria arquivo vazio (pede o nome) |
| `p` | cria pasta (pede o nome) |
| `r` | renomeia (pede o novo nome) |
| `d` | apaga; pede confirmação com `s` e Enter |
| `c` `m` | pede o destino ("Copiar para:" ou "Mover para:") e copia ou move a seleção |
| `q` ou ESC | sai |

As perguntas são campos de texto: digite a resposta e confirme com Enter.
Para `d`, digite `s` e Enter. ESC cancela a pergunta.

## Tela

A tela é de texto VGA, 80 colunas por 25 linhas. O editor e o gerenciador
ocupam a tela inteira e redesenham ao mudar. Mensagens da serial COM1 saem
em paralelo, para quem acompanha pelo terminal da máquina virtual.
