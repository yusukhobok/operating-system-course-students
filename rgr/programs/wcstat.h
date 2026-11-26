/* wcstat — сквозная программа РГР «Статистика файла»: считает строки, слова и байты (как wc).
   Одна программа, режимы добавляются от работы к работе; код режима — в своём файле:
     wcstat.c   — основной режим, без параметров режима (ЛР 5 и далее используют его подсчёт)
     procs.c    — потомки, канал, сигналы        (ЛР 5, 6, 7)
     threads.c  — потоки, мьютекс, очередь       (ЛР 8, 9)
     mem.c      — память                         (ЛР 10)
     ipc.c      — сервер и клиент, очередь       (ЛР 11)   */
#ifndef WCSTAT_H
#define WCSTAT_H

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct Total {
    long lines, words, bytes;
};

struct Options {
    int block;      /* -b  размер блока чтения, байт */
    int pause_ms;   /* -s  пауза после каждого блока, мс */
    int hold;       /* -h  сколько секунд ждать в конце работы */
    int procs;      /* -p  число потомков */
    int nowait;     /* -n  родитель не вызывает wait */
    int use_pipe;   /* -c  потомки отдают результат родителю через канал */
    int rec;        /* -r  размер записи в канале, байт */
    int delay;      /* -d  родитель начинает читать канал через столько секунд */
    int leak;       /* -l  родитель не закрывает свой записывающий конец канала */
    int mask;       /* -m  заблокировать сигналы на время работы */
    int threads;    /* -t  число потоков */
    int mutex;      /* -x  общий итог обновляется под мьютексом */
    int queue;      /* -q  конвейер: читатель и потоки через очередь из Q блоков */
    int deadlock;   /* -D  нечётные потоки конвейера берут мьютексы в другом порядке */
    int mem;        /* -M  0 — блоками, 1 — весь файл в malloc, 2 — mmap */
    int cow;        /* -w  потомок пишет в память */
    int serve;      /* -S  сервер */
    const char *ask;/* -a  клиент: файл, статистику которого просим у сервера */
    long key;       /* -k  ключ очереди сообщений */
    const char *file;
};

extern struct Options opt;
extern volatile sig_atomic_t stop;   /* номер сигнала, по которому просили остановиться */
extern volatile sig_atomic_t usr1_count, chld_count;   /* сколько получено SIGUSR1 и SIGCHLD */
void notice(void);                   /* сообщить о полученных SIGUSR1 (procs.c) */

void count_block(const char *buf, int n, int *inword, struct Total *t);
int inword_before(int fd, long pos);
int count_range(int fd, long from, long to, struct Total *t);
void pause_block(void);
void hold(int seconds);
double now(void);
void print_total(const char *what, const struct Total *t);

int run_procs(int fd, long size);
int run_threads(int fd, long size);
int run_queue(int fd, long size);
int run_mem(int fd, long size);
int run_server(void);
int run_client(void);

#endif
