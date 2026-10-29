/* ЛР 11. Два независимых процесса, связанных очередью сообщений System V.
     wcstat -S [-k ключ]            сервер: принимает запросы «посчитать файл», отвечает статистикой
     wcstat -a файл [-k ключ]       клиент: просит сервер посчитать файл и ждёт ответ
   Запрос — сообщение типа 1; ответ — сообщение, тип которого равен PID клиента. */
#include "wcstat.h"
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>

struct Request {
    long mtype;
    long client;
    char path[256];
};

struct Reply {
    long mtype;
    long lines, words, bytes;
};

#define REQ_SIZE (sizeof(struct Request) - sizeof(long))
#define REP_SIZE (sizeof(struct Reply) - sizeof(long))

static void on_term(int sig)
{
    stop = sig;
}

int run_server(void)
{
    struct sigaction sa;
    int qid = msgget((key_t)opt.key, IPC_CREAT | 0600);

    if (qid == -1) {
        perror("msgget");
        return 1;
    }
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    printf("сервер PID %d: очередь %d, ключ 0x%lx\n", getpid(), qid, opt.key);
    fflush(stdout);

    while (!stop) {
        struct Request rq;
        struct Reply rp = { 0, -1, -1, -1 };
        struct Total t = { 0, 0, 0 };
        struct stat st;
        int fd;

        if (msgrcv(qid, &rq, REQ_SIZE, 1, 0) == -1) {
            if (errno == EINTR)
                continue;
            perror("msgrcv");
            break;
        }
        rq.path[sizeof rq.path - 1] = '\0';
        opt.file = rq.path;
        fd = open(rq.path, O_RDONLY);
        if (fd != -1 && fstat(fd, &st) == 0 && count_range(fd, 0, st.st_size, &t) == 0) {
            rp.lines = t.lines;
            rp.words = t.words;
            rp.bytes = t.bytes;
        } else {
            perror(rq.path);
        }
        if (fd != -1)
            close(fd);
        printf("сервер: клиент %ld, файл %s: строк %ld, слов %ld, байт %ld\n", rq.client, rq.path,
               rp.lines, rp.words, rp.bytes);
        fflush(stdout);
        rp.mtype = rq.client;
        if (msgsnd(qid, &rp, REP_SIZE, 0) == -1)
            perror("msgsnd");
    }
    msgctl(qid, IPC_RMID, NULL);
    printf("сервер: остановлен сигналом %d, очередь удалена\n", (int)stop);
    return 0;
}

int run_client(void)
{
    struct Request rq;
    struct Reply rp;
    char path[256];
    int qid = msgget((key_t)opt.key, 0);

    if (qid == -1) {
        perror("msgget (очереди нет — сервер запущен?)");
        return 1;
    }
    if (realpath(opt.ask, path) == NULL) {
        perror(opt.ask);
        return 1;
    }
    memset(&rq, 0, sizeof rq);
    rq.mtype = 1;
    rq.client = getpid();
    strncpy(rq.path, path, sizeof rq.path - 1);
    if (msgsnd(qid, &rq, REQ_SIZE, 0) == -1) {
        perror("msgsnd");
        return 1;
    }
    printf("клиент PID %d: запрос на %s отправлен, жду ответ\n", getpid(), path);
    fflush(stdout);
    if (msgrcv(qid, &rp, REP_SIZE, getpid(), 0) == -1) {
        perror("msgrcv");
        return 1;
    }
    printf("клиент: строк %ld, слов %ld, байт %ld\n", rp.lines, rp.words, rp.bytes);
    return 0;
}
