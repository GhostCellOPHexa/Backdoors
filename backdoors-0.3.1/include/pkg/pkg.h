/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_PKG_PKG_H
#define BD_PKG_PKG_H

/*
 * Gerenciador de pacotes (versão 0.3.1). Pacote .bkg = arquivo cpio "newc"
 * com a entrada PKG/INFO (nome, versao, descricao) e os arquivos a partir da
 * raiz. O registro dos instalados fica em /var/lib/pkg/NOME.
 *
 * Sem rede nesta versão: 'update' instala de um diretório local (ex.: /repo).
 * Um instalado nunca é sobrescrito por arquivo de outro pacote.
 */

/* Instala um .bkg (caminho no VFS). Atualiza se o nome já estiver instalado. Retorna 0 ou -1. */
int pkg_install(const char *archive);

/* Remove o pacote pelo nome, apagando os arquivos registrados. Retorna 0 ou -1. */
int pkg_remove(const char *name);

/* Lista os pacotes instalados (nome e versão). */
void pkg_list(void);

/* Mostra PKG/INFO e a quantidade de arquivos de um .bkg. Retorna 0 ou -1. */
int pkg_info(const char *archive);

/* Instala de um diretório: todo .bkg que for novo ou de outra versão. Retorna 0 ou -1. */
int pkg_update(const char *dir);

#endif /* BD_PKG_PKG_H */
