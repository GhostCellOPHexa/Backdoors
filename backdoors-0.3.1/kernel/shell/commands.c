/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdint.h>

#include "arch/cpu.h"
#include "arch/io.h"
#include "drivers/keyboard.h"
#include "drivers/rtc.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/vga_text.h"
#include "kernel/log.h"
#include "kernel/panic.h"
#include "kernel/version.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "shell/builtins.h"
#include "shell/commands.h"
#include "shell/script.h"

#define TEXT_COLOR VGA_WHITE_ON_BLACK

typedef void (*command_fn)(int argc, char **argv);

struct command {
    const char *name;
    command_fn  fn;         /* NULL: comando ainda não implementado */
    const char *summary;    /* descrição curta mostrada em help */
    const char *planned;    /* motivo do "indisponível" quando fn == NULL */
};

static int str_equal(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* ---------- comandos do sistema ---------- */

static void cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    vga_clear();
    /* Sequência ANSI para terminais seriais (ex.: minicom, PuTTY). */
    klog("\033[2J\033[H");
}

static void cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kprintf("%s %s (i686)\n", BD_OS_NAME, BD_VERSION);
}

static void cmd_about(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kprintf("Backdoors OS %s: sistema operacional livre e open source (GPL-3.0-or-later).\n",
            BD_VERSION);
    kprintf("Kernel monolitico em C e NASM para i686, interface de texto.\n");
    kprintf("Inclui: shell com scripts, sistema de arquivos em memoria, editor,\n");
    kprintf("gerenciador de arquivos, linguagem .int, subconjunto de Python e pacotes .bkg.\n");
}

static void cmd_help(int argc, char **argv);

static void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        kprintf("%s%s", argv[i], (i + 1 < argc) ? " " : "");
    }
    kprintf("\n");
}

static void cmd_date(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    struct rtc_time t;
    if (!rtc_read(&t)) {
        kprintf("date: relogio indisponivel\n");
        return;
    }
    kprintf("%04u-%02u-%02u %02u:%02u:%02u (RTC)\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second);
}

static void cmd_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t hz = timer_frequency();
    uint32_t total = hz ? timer_ticks() / hz : 0;
    kprintf("up %u:%02u:%02u (%u ticks)\n",
            total / 3600, (total / 60) % 60, total % 60, timer_ticks());
}

static void cmd_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t total_frames = pmm_total_frames();
    uint32_t free_frames  = pmm_free_frames();
    kprintf("fisica: %u KiB total, %u KiB livre, %u KiB em uso (frames de 4 KiB)\n",
            total_frames * 4u, free_frames * 4u, (total_frames - free_frames) * 4u);

    struct heap_stats s;
    heap_get_stats(&s);
    kprintf("heap:   %u KiB total, %u bytes em uso, %u bytes livres, %u blocos\n",
            (uint32_t)(s.total / 1024u), (uint32_t)s.used, (uint32_t)s.free, (uint32_t)s.blocks);
    if (s.errors != 0) {
        kprintf("heap:   %u erro(s) detectado(s) desde o boot\n", (uint32_t)s.errors);
    }
}

static void cmd_cpu(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!cpu_has_cpuid()) {
        kprintf("CPUID nao suportado: CPU anterior ao 486.\n");
        return;
    }

    uint32_t regs[4];
    cpu_cpuid(0, regs);
    char vendor[13];
    /* Ordem da string de fabricante: EBX, EDX, ECX (4 bytes cada, little-endian). */
    uint32_t parts[3] = { regs[1], regs[3], regs[2] };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            vendor[i * 4 + j] = (char)((parts[i] >> (8 * j)) & 0xFF);
        }
    }
    vendor[12] = '\0';
    kprintf("vendor: %s\n", vendor);

    cpu_cpuid(1, regs);
    uint32_t eax = regs[0];
    uint32_t family   = (eax >> 8) & 0xF;
    uint32_t model    = (eax >> 4) & 0xF;
    uint32_t stepping = eax & 0xF;
    if (family == 6 || family == 15) {
        model |= ((eax >> 16) & 0xF) << 4;      /* modelo estendido */
    }
    if (family == 15) {
        family += (eax >> 20) & 0xFF;           /* família estendida */
    }
    kprintf("family %u model %u stepping %u\n", family, model, stepping);

    uint32_t edx = regs[3];
    kprintf("features:");
    if (edx & (1u << 0))  kprintf(" fpu");
    if (edx & (1u << 4))  kprintf(" tsc");
    if (edx & (1u << 5))  kprintf(" msr");
    if (edx & (1u << 6))  kprintf(" pae");
    if (edx & (1u << 9))  kprintf(" apic");
    if (edx & (1u << 25)) kprintf(" sse");
    if (edx & (1u << 26)) kprintf(" sse2");
    kprintf("\n");
}

static void cmd_devices(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kprintf("serial    COM1 0x3F8, 115200 8N1      %s\n",
            serial_available() ? "ok" : "indisponivel");
    kprintf("vga       texto 80x25 em 0xB8000     ok\n");
    kprintf("keyboard  PS/2, IRQ1                 %s\n",
            keyboard_available() ? "ok" : "indisponivel");
    kprintf("timer     PIT canal 0, IRQ0, %u Hz  %s\n",
            timer_frequency(), timer_frequency() ? "ok" : "indisponivel");
    kprintf("pic       8259 em cascata, vetores 32..47   ok\n");
    kprintf("memoria   PMM por bitmap, heap de 4 MiB (mem)\n");
    kprintf("disco     nenhum: sistema de arquivos em memoria (sem persistencia)\n");
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kprintf("rebooting...\n");
    /* Pulso de reset pelo controlador de teclado (porta 0x64, comando 0xFE). */
    outb(0x64, 0xFE);
    /* Se o comando não reiniciar a máquina, voltamos ao shell e informamos. */
    kprintf("reboot: o controlador nao executou o reset\n");
}

static void cmd_panic_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#ifdef BD_ENABLE_PANIC_TEST
    kpanic("panic-test command");
#else
    kprintf("panic-test: desabilitado nesta build (compile com BD_ENABLE_PANIC_TEST)\n");
#endif
}

/* ---------- tabela ---------- */

static const struct command table[] = {
    { "help",      cmd_help,       "lista os comandos",                        NULL },
    { "clear",     cmd_clear,      "limpa a tela",                             NULL },
    { "version",   cmd_version,    "versao do sistema",                        NULL },
    { "about",     cmd_about,      "informacoes sobre o projeto",              NULL },
    { "echo",      cmd_echo,       "imprime os argumentos",                    NULL },
    { "set",       cmd_set,        "define variavel: set NOME VALOR",          NULL },
    { "date",      cmd_date,       "data e hora (RTC)",                        NULL },
    { "uptime",    cmd_uptime,     "tempo desde o boot",                       NULL },
    { "mem",       cmd_mem,        "memoria fisica e heap",                    NULL },
    { "cpu",       cmd_cpu,        "identificacao da CPU",                     NULL },
    { "devices",   cmd_devices,    "dispositivos inicializados",               NULL },
    { "ls",        cmd_ls,         "lista diretorio (ls -l para detalhes)",    NULL },
    { "cd",        cmd_cd,         "muda o diretorio atual",                   NULL },
    { "pwd",       cmd_pwd,        "mostra o diretorio atual",                 NULL },
    { "mkdir",     cmd_mkdir,      "cria diretorio",                           NULL },
    { "touch",     cmd_touch,      "cria arquivo vazio",                       NULL },
    { "cat",       cmd_cat,        "mostra o conteudo de arquivos",            NULL },
    { "write",     cmd_write,      "grava texto em arquivo (sobrescreve)",     NULL },
    { "append",    cmd_append,     "acrescenta texto ao arquivo",              NULL },
    { "rm",        cmd_rm,         "remove arquivo (nao apaga diretorios)",    NULL },
    { "rmdir",     cmd_rmdir,      "remove diretorio vazio",                   NULL },
    { "cp",        cmd_cp,         "copia arquivo",                            NULL },
    { "mv",        cmd_mv,         "move ou renomeia",                         NULL },
    { "tree",      cmd_tree,       "mostra a arvore de diretorios",            NULL },
    { "edit",      cmd_edit,       "editor de texto (estilo vi)",              NULL },
    { "vi",        cmd_edit,       "o mesmo que edit",                         NULL },
    { "files",     cmd_files,      "gerenciador de arquivos",                  NULL },
    { "python",    cmd_python,     "executa Python (subconjunto)",             NULL },
    { "pyc",       cmd_pyc,        "compila Python para arquivo .int",         NULL },
    { "int",       cmd_int,        "executa um arquivo .int",                  NULL },
    { "intinfo",   cmd_intinfo,    "mostra o cabecalho de um .int",            NULL },
    { "pkg",       cmd_pkg,        "pacotes: install, remove, list, info, update", NULL },
    { "sh",        cmd_sh,         "executa um script de comandos",            NULL },
    { "reboot",    cmd_reboot,     "reinicia a maquina",                       NULL },
    { "panic-test", cmd_panic_test, "teste de panico (desabilitado por padrao)", NULL },
    { "mount",     NULL,           "montar sistema de arquivos",
      "requer armazenamento persistente (fase 8)" },
    { "umount",    NULL,           "desmontar sistema de arquivos",
      "requer armazenamento persistente (fase 8)" },
    { "tasks",     NULL,           "lista de tarefas",
      "requer escalonador e processos (fase 9)" },
    { "ps",        NULL,           "processos",
      "requer escalonador e processos (fase 9)" },
    { "kill",      NULL,           "encerrar processo",
      "requer escalonador e processos (fase 9)" },
};

static size_t text_length(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const size_t count = sizeof(table) / sizeof(table[0]);
    kprintf("Comandos disponiveis:\n");
    for (size_t i = 0; i < count; i++) {
        if (table[i].fn == NULL) {
            continue;
        }
        kprintf("  %s", table[i].name);
        for (size_t p = text_length(table[i].name); p < 14; p++) {
            kprintf(" ");
        }
        kprintf("%s\n", table[i].summary);
    }
    kprintf("Indisponiveis ate as proximas fases: mount, umount, tasks, ps, kill.\n");
    kprintf("Scripts: sh ARQUIVO. Variaveis: set NOME VALOR, depois use $NOME.\n");
}

void commands_dispatch(int argc, char **argv)
{
    const size_t count = sizeof(table) / sizeof(table[0]);

    for (size_t i = 0; i < count; i++) {
        if (!str_equal(table[i].name, argv[0])) {
            continue;
        }
        if (table[i].fn != NULL) {
            table[i].fn(argc, argv);
        } else {
            kprintf("%s: indisponivel: %s\n", table[i].name, table[i].planned);
        }
        return;
    }

    vga_set_color(VGA_LIGHT_RED);
    kprintf("%s: comando nao encontrado (digite help)\n", argv[0]);
    vga_set_color(TEXT_COLOR);
}
