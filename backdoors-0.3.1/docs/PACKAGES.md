# Pacotes `.bkg` e atualizações

Um pacote do Backdoors OS é um arquivo `.bkg`. Ele é um arquivo cpio no
formato `newc` (o mesmo do initramfs) com uma entrada de metadados e os
arquivos a instalar. A ferramenta de geração e o gerenciador `pkg` estão no
sistema; a instalação é feita pelo shell do próprio Backdoors OS.

Não há rede: nesta versão a "atualização" lê pacotes de um diretório local
(o repositório). Baixar pacotes pela internet depende de driver de rede,
que ainda não existe.

## Formato do `.bkg`

A primeira entrada é `PKG/INFO`, um texto de linhas `chave=valor`:

```
nome=hello
versao=1.0.0
descricao=Pacote de exemplo
```

- `nome`: letras minúsculas, dígitos, `_` e `-`, de 1 a 24 caracteres.
- `versao`: letras, dígitos, `.`, `_`, `+` e `-`, de 1 a 23 caracteres.
- `descricao`: uma linha, até 60 caracteres (opcional).

As demais entradas são os arquivos do pacote, com caminho **relativo à
raiz** do sistema (`home/bemvindo.py`, `usr/share/hello/LEIA-ME.txt`).
Caminhos absolutos e com `..` são recusados.

## Gerar um pacote

Na máquina de desenvolvimento (precisa de `python3`):

```sh
python3 tools/mkpkg.py packages/hello hello 1.0.0 build/hello-1.0.0.bkg "Pacote de exemplo"
```

O primeiro argumento é a pasta de origem. Tudo o que estiver dentro dela
vai para o pacote, com o caminho relativo a essa pasta.

## Registro de pacotes instalados

Cada pacote instalado tem um arquivo em `/var/lib/pkg/NOME` com:

```
nome=hello
versao=1.0.0
arquivo=/home/bemvindo.py
arquivo=/usr/share/hello/LEIA-ME.txt
```

O registro é a fonte da verdade para remoção e atualização. Só são listados
no registro arquivos instalados pelo próprio pacote.

## Comandos

| Comando | Efeito |
|---------|--------|
| `pkg install ARQUIVO.bkg` | instala; se a mesma versão já estiver instalada, avisa e não faz nada |
| `pkg install ARQUIVO.bkg` (outra versão) | troca a versão: remove os arquivos antigos do registro e instala os novos |
| `pkg remove NOME` | remove os arquivos registrados e o registro |
| `pkg list` | lista os pacotes instalados com a versão |
| `pkg info ARQUIVO.bkg` | mostra nome, versão, descrição e número de arquivos de um `.bkg` |
| `pkg update [DIRETORIO]` | instala ou atualiza todos os `.bkg` do diretório (padrão: `/repo`) |

`pkg info` lê o arquivo `.bkg`, não um nome instalado.

Qualquer versão diferente da instalada substitui a atual. O sistema não
compara qual versão é mais nova: não há regra de downgrade, então instalar
uma versão antiga por engano também troca a versão.

## Regras de segurança da instalação

1. O pacote é lido e validado inteiro antes de qualquer mudança: `PKG/INFO`
   presente, nome e versão válidos.
2. Se algum arquivo do pacote já existir e **não** pertencer ao pacote (por
   exemplo, um arquivo criado por você), a instalação é recusada e nada muda:

   ```
   pkg: conflito: /home/bemvindo.py ja existe e nao pertence a este pacote
   pkg: instalacao cancelada, nada foi alterado
   ```

3. Se a extração falhar no meio, o pacote fica parcialmente instalado e o
   sistema diz para usar `pkg remove NOME`.

## Repositório e atualização

`pkg update` lê o diretório (padrão `/repo`), instala cada `.bkg` que
encontrar e mostra um resumo:

```
pkg: repositorio /repo: 1 pacote(s), 0 com erro
```

O initramfs inclui `/repo/hello-1.0.0.bkg`. Para publicar uma atualização
nesta versão, coloque o arquivo `.bkg` da nova versão no diretório do repositório
(por exemplo, numa nova ISO, ou copiando-o com `cp`), e rode `pkg update`.

Limites: até 32 nomes de pacote por repositório, registro de até 16 KiB,
`PKG/INFO` de até 512 bytes.

## O que ainda não existe

- Download pela rede e servidor de repositório (depende do driver de rede).
- Dependências entre pacotes: cada pacote é independente.
- Assinatura digital dos pacotes: a origem é a do diretório confiável.
- Armazenamento persistente: pacotes instalados existem só até reiniciar,
  até a Fase 8.
