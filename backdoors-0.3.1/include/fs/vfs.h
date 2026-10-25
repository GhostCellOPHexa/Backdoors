/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_FS_VFS_H
#define BD_FS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Sistema de arquivos em memória (Fase 5). A árvore fica no heap do kernel:
 * é criada no boot a partir do initramfs (módulo cpio do GRUB) e alterada pelo
 * shell, pelo editor e pelo gerenciador de arquivos. NADA é gravado em disco:
 * as mudanças somem ao reiniciar até a Fase 8 (armazenamento persistente).
 */

#define VFS_NAME_MAX  31
#define VFS_PATH_MAX  255
#define VFS_FILE_MAX  (64u * 1024u)     /* tamanho máximo de um arquivo */

enum vfs_status {
    VFS_OK = 0,
    VFS_ENOENT,         /* não existe */
    VFS_EEXIST,         /* já existe */
    VFS_ENOTDIR,        /* componente do caminho não é diretório */
    VFS_EISDIR,         /* operação exige arquivo e o alvo é diretório */
    VFS_ENOTEMPTY,      /* diretório não está vazio */
    VFS_EINVAL,         /* nome ou caminho inválido */
    VFS_ENAMETOOLONG,   /* nome ou caminho longo demais */
    VFS_EFBIG,          /* arquivo maior que VFS_FILE_MAX */
    VFS_ENOMEM,         /* heap esgotado */
    VFS_EBUSY,          /* diretório em uso (é o atual ou ancestral dele) */
};

/* Mensagem curta, em português ASCII, para o status. */
const char *vfs_strerror(int status);

struct vfs_stat {
    char name[VFS_NAME_MAX + 1];
    bool is_dir;
    size_t size;        /* bytes (0 para diretórios) */
};

/* Callback de vfs_list: 'name' é o nome do filho; retorne false para parar. */
typedef bool (*vfs_list_fn)(const char *name, bool is_dir, size_t size, void *ctx);

/* Cria a raiz '/' vazia e posiciona o diretório atual em '/'. */
void vfs_init(void);

/*
 * Carrega um arquivo cpio no formato "newc" (initramfs). Cria diretórios e
 * arquivos a partir da raiz. Retorna o número de entradas carregadas ou -1 se
 * o arquivo estiver malformado.
 */
int vfs_load_cpio(const uint8_t *data, size_t len);

/*
 * Percorre as entradas de um arquivo cpio "newc" em memória. 'fn' recebe o
 * nome bruto da entrada, o modo (tipo em 0170000), o conteúdo e o tamanho;
 * deve devolver 0 para continuar ou outro valor para abortar (a função
 * devolve -1). Retorna o número de entradas percorridas ou -1 se malformado.
 */
typedef int (*vfs_cpio_fn)(const char *name, uint32_t mode, const uint8_t *body,
                           size_t size, void *ctx);
int vfs_cpio_walk(const uint8_t *data, size_t len, vfs_cpio_fn fn, void *ctx);

/* Operações. Caminhos absolutos ('/a/b') ou relativos ao diretório atual. */
int vfs_stat(const char *path, struct vfs_stat *st);
int vfs_list(const char *path, vfs_list_fn fn, void *ctx);
int vfs_read(const char *path, size_t offset, void *buf, size_t n, size_t *got);
int vfs_mkdir(const char *path);
int vfs_touch(const char *path);
int vfs_write_file(const char *path, const void *data, size_t len);
int vfs_append_file(const char *path, const void *data, size_t len);
int vfs_remove(const char *path);          /* só arquivos; diretórios usam vfs_rmdir */
int vfs_rmdir(const char *path);           /* só diretórios vazios e fora do caminho atual */
int vfs_rename(const char *src, const char *dst);   /* mv: se dst for diretório, move para dentro */
int vfs_copy(const char *src, const char *dst);     /* cp: só arquivos */
int vfs_chdir(const char *path);
int vfs_getcwd(char *buf, size_t cap);

#endif /* BD_FS_VFS_H */
