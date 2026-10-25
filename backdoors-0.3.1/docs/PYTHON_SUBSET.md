# Subconjunto de Python do Backdoors OS

O Backdoors OS executa um subconjunto pequeno de Python. O código-fonte
(`.py`) é compilado em uma só passagem para o formato `.int` (veja
[INT_FORMAT.md](INT_FORMAT.md)) e executado pela máquina virtual do sistema.
Não há interpretador completo, nem biblioteca padrão, nem módulos.

Comandos que usam este subconjunto:

- `python ARQUIVO.py` compila e executa.
- `python -c "codigo"` compila e executa uma linha.
- `pyc ARQUIVO.py [SAIDA.int]` compila para `.int` sem executar.
- `int ARQUIVO.int` executa um `.int` já compilado.
- `edit` com `:int` (compila o buffer) e `:run` (compila e executa).

ESC interrompe a execução de qualquer programa.

## Tipos de valor

| Tipo | Exemplos | Observação |
|------|----------|------------|
| inteiro | `0`, `42`, `-7` | 32 bits com sinal; estouro dá a volta (complemento de dois) |
| booleano | `True`, `False` | resultado de comparações e de `and`, `or`, `not` |
| texto | `"ola"`, `'ola'` | até 255 bytes por literal; escapes `\n`, `\t`, `\\`, `\'`, `\"` |

Não há ponto flutuante, nem listas, dicionários, tuplas ou `None`.

## Sintaxe suportada

### Atribuição e expressões

```python
x = 10
y = x * 2 + 1
nome = "Backdoors"
msg = "ola " + nome        # + concatena textos
```

- Operadores aritméticos: `+`, `-` (binário e unário), `*`, `//` (divisão
  inteira com arredondamento para baixo, como no Python), `%` (resto com o
  sinal do divisor, como no Python).
- Comparações: `==`, `!=`, `<`, `<=`, `>`, `>=`. Não são encadeadas:
  `a < b < c` dá erro; escreva `a < b and b < c`.
- Lógicos: `and`, `or`, `not`, com avaliação curta-circuito.
- Parênteses para agrupar.

Uma variável precisa ser atribuída no texto do programa **antes** de ser
usada. A ordem é a do texto, sem análise de fluxo.

### Controle de fluxo

```python
if x > 10:
    print("grande")
elif x > 5:
    print("medio")
else:
    print("pequeno")

while x < 3:
    x = x + 1

for i in range(1, 4):       # 1, 2, 3
    print("contagem", i)

for i in range(10):         # 0 a 9
    if i == 5:
        break
    if i % 2 == 0:
        continue
    print(i)
```

- `range` aceita `range(fim)` ou `range(início, fim)`. O fim é exclusivo,
  como no Python. Não há passo (terceiro argumento): o laço sempre cresce de
  1 em 1. O `for` só aceita `for NOME in range(...)`.
- `break` e `continue` valem para o laço mais interno.
- `pass` é uma instrução que não faz nada.
- Laços podem ser aninhados até 8 níveis.

### Funções embutidas

| Função | Efeito |
|--------|--------|
| `print(a, b, ...)` | escreve os argumentos separados por espaço, seguidos de quebra de linha |
| `len(texto)` | número de bytes do texto |
| `str(x)` | converte inteiro ou booleano em texto |
| `int(x)` | converte texto em inteiro (texto inválido dá erro de execução) |

Funções embutidas recebem exatamente um argumento, exceto `print`.

### Comentários

Tudo depois de `#` até o fim da linha é ignorado.

### Indentação

Blocos usam espaços (não tabulações). A indentação deve ser consistente
dentro do bloco. Uma expressão quebrada entre parênteses pode continuar em
outra linha.

## Não suportado

Estas construções dão erro de compilação com o número da linha:

- `def`, `class`, `return`, `lambda`, `import`, `from`, `with`, `try`,
  `except`, `finally`, `raise`, `yield`, `global`, `nonlocal`, `del`,
  `assert`, `async`, `await`, `as`, `is` e `None`. Funções e classes
  não existem nesta versão.
- Divisão real `/` e potência `**`.
- Chamada de métodos (`"texto".upper()`), listas, dicionários, fatiamento
  e `in` fora do `for ... in range(...)`.
- Strings de várias linhas (`"""..."""`) e f-strings.
- Comparação encadeada (`a < b < c`).
- Tabulações na indentação.

## Limites

| Item | Limite |
|------|--------|
| código gerado por programa | 32 KiB |
| tabela de textos constantes | 4 KiB |
| variáveis por programa | 64 |
| texto de um literal | 255 bytes |
| profundidade de laços aninhados | 8 |
| `break`/`continue` por laço | 32 |
| pilha da máquina virtual | 128 valores |
| memória de textos em tempo de execução | 4 KiB |

Programas que estouram um limite recebem uma mensagem em português, com a
linha do erro. Estouro da pilha, da memória de textos ou um `int()` inválido
interrompem a execução com erro, sem derrubar o sistema.

## Exemplo completo

```python
# Soma de 1 a 10, com verificação.
total = 0
for i in range(1, 11):
    total = total + i
print("soma de 1 a 10 =", total)
if total == 55:
    print("correto")
else:
    print("errado")
```

Saída:

```
soma de 1 a 10 = 55
correto
```

O arquivo `home/somas.py` do initramfs contém esse exemplo.
