# Avisos de terceiros

O código próprio do Backdoors OS é licenciado sob GPL-3.0-or-later (veja [LICENSE](LICENSE)).
Este arquivo lista o que vem de terceiros, usado no build ou incluído nas saídas.

## Incluído na ISO gerada

### GRUB 2

- **Uso:** bootloader da ISO. O `grub-mkrescue` copia os módulos e a imagem de boot do GRUB instalados no sistema para dentro de `build/backdoors.iso`.
- **Licença:** GNU GPL versão 3 ou posterior.
- **Distribuição:** ao redistribuir a ISO, é preciso respeitar a GPL do GRUB, incluindo disponibilizar o código-fonte correspondente.
- **Observação:** o projeto não copia código do GRUB para o repositório.

## Usado no build, não redistribuído pelo projeto

Estas ferramentas são necessárias para compilar e testar, mas não são copiadas para o repositório nem para a ISO:

- **GCC** e seus headers freestanding (`stdint.h`, `stddef.h`, `stdarg.h`, `stdbool.h`). Os headers vêm da instalação do compilador e são usados, não copiados.
- **NASM**, **GNU binutils** (`ld`), **GNU Make**, **xorriso**, **mtools**.
- **QEMU** (usado nos testes).
- **Python 3** (verificador de saída de boot).

As licenças dessas ferramentas são as da sua distribuição. Ao instalá-las, siga os termos delas.

## Código copiado para o repositório

Nenhum código de terceiros foi copiado para o repositório na versão 0.1.0. Todo o código em `kernel/`, `include/`, `boot/`, `entry/`, `iso/` e `tests/` foi escrito para este projeto.

Quando código de terceiros for adicionado no futuro, este arquivo deve ser atualizado com origem, licença e cópia do aviso.
