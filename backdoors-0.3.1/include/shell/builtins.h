/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_SHELL_BUILTINS_H
#define BD_SHELL_BUILTINS_H

/* Comandos embutidos do shell. Cada um recebe argc/argv como o shell os separou. */

/* Sistema de arquivos (cmd_fs.c). */
void cmd_ls(int argc, char **argv);
void cmd_cd(int argc, char **argv);
void cmd_pwd(int argc, char **argv);
void cmd_mkdir(int argc, char **argv);
void cmd_touch(int argc, char **argv);
void cmd_cat(int argc, char **argv);
void cmd_write(int argc, char **argv);
void cmd_append(int argc, char **argv);
void cmd_rm(int argc, char **argv);
void cmd_rmdir(int argc, char **argv);
void cmd_cp(int argc, char **argv);
void cmd_mv(int argc, char **argv);
void cmd_tree(int argc, char **argv);

/* Linguagens e programas (cmd_lang.c). */
void cmd_python(int argc, char **argv);
void cmd_pyc(int argc, char **argv);
void cmd_int(int argc, char **argv);
void cmd_intinfo(int argc, char **argv);

/* Aplicativos de tela cheia (cmd_apps.c). */
void cmd_edit(int argc, char **argv);
void cmd_files(int argc, char **argv);

/* Pacotes e scripts (cmd_pkg.c). */
void cmd_pkg(int argc, char **argv);
void cmd_sh(int argc, char **argv);
void cmd_set(int argc, char **argv);

#endif /* BD_SHELL_BUILTINS_H */
