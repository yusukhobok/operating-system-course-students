/* ЛР 5, 6, 7. wcstat -p K: файл делится на K частей, каждую считает свой потомок (fork).
     -n   родитель не вызывает wait (потомки становятся зомби)               — ЛР 5
     -c   потомки отдают результат родителю через канал; -r, -d, -l          — ЛР 6
     -m   сигналы INT, TERM, USR1 заблокированы на время работы             — ЛР 7
   Обработчики сигналов (SIGINT, SIGTERM — остановиться; SIGUSR1 — отметить; SIGCHLD — посчитать)
   установлены всегда и наследуются потомками. */
#include "wcstat.h"
#include <sys/wait.h>

volatile sig_atomic_t usr1_count, chld_count;
static int usr1_shown;

static void on_signal(int sig)
{
    if (sig == SIGUSR1)
        usr1_count++;
    else if (sig == SIGCHLD)
        chld_count++;
    else
        stop = sig;
}

/* Сообщает о полученных SIGUSR1. Вне обработчика: printf в обработчике небезопасен. */
void notice(void)
{
    while (usr1_shown < usr1_count) {
        usr1_shown++;
        printf("процесс %d: получен SIGUSR1 (%d-й)\n", getpid(), usr1_shown);
        fflush(stdout);
    }
}

static void install_handlers(void)
{
    struct sigaction sa;
    int sigs[] = { SIGINT, SIGTERM, SIGUSR1, SIGCHLD };

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    for (int i = 0; i < 4; i++) {
        sa.sa_flags = sigs[i] == SIGCHLD ? SA_NOCLDSTOP : 0;
        if (sigaction(sigs[i], &sa, NULL) == -1)
            perror("sigaction");
    }
}

/* Заблокировать (SIG_BLOCK) или разблокировать (SIG_UNBLOCK) сигналы INT, TERM, USR1. */
static void set_mask(int how)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGUSR1);
    sigprocmask(how, &set, NULL);
}

/* Одна запись канала: ровно opt.rec байт, «номер строк слов байт», остальное — пробелы и '\n'. */
static int send_record(int wfd, int part, const struct Total *d)
{
    char rec[4096], text[64];
    int n = snprintf(text, sizeof text, "%d %ld %ld %ld", part, d->lines, d->words, d->bytes);

    memset(rec, ' ', opt.rec);
    memcpy(rec, text, n);
    rec[opt.rec - 1] = '\n';
    while (write(wfd, rec, opt.rec) == -1) {
        if (errno != EINTR) {
            perror("write");
            return -1;
        }
        notice();
        if (stop)
            return -1;
    }
    return 0;
}

static void child(int part, int fd, long from, long to, int wfd)
{
    char *buf = malloc(opt.block);
    struct Total t = { 0, 0, 0 };
    int inword = inword_before(fd, from);
    long pos = from;

    if (buf == NULL)
        _exit(1);
    while (pos < to && !stop) {
        long want = to - pos < opt.block ? to - pos : opt.block;
        ssize_t n = pread(fd, buf, want, pos);
        struct Total d = { 0, 0, 0 };
        if (n <= 0) {
            perror(opt.file);
            _exit(1);
        }
        count_block(buf, n, &inword, &d);
        t.lines += d.lines;
        t.words += d.words;
        t.bytes += d.bytes;
        if (opt.use_pipe && send_record(wfd, part, &d) == -1)
            break;
        pos += n;
        notice();
        pause_block();
    }
    if (opt.mask)
        set_mask(SIG_UNBLOCK);          /* отложенные сигналы доставляются здесь */
    notice();
    if (stop)
        printf("потомок %d (PID %d): остановлен сигналом %d, обработано %ld из %ld байт\n",
               part, getpid(), (int)stop, pos - from, to - from);
    else if (!opt.use_pipe)
        printf("потомок %d (PID %d): строк %ld, слов %ld, байт %ld\n", part, getpid(), t.lines,
               t.words, t.bytes);
    fflush(stdout);
    _exit(stop ? 3 : 0);
}

/* Читает записи канала, пока не закроются все записывающие концы (read вернёт 0). */
static void read_records(int rfd, struct Total *sum, long *records)
{
    char rec[4096];

    hold(opt.delay);
    while (!stop) {
        ssize_t n = read(rfd, rec, opt.rec);
        int part;
        struct Total d;
        if (n == -1 && errno == EINTR) {
            notice();
            continue;
        }
        if (n <= 0)
            break;
        if (n != opt.rec || sscanf(rec, "%d %ld %ld %ld", &part, &d.lines, &d.words, &d.bytes) != 4) {
            fprintf(stderr, "wcstat: испорченная запись\n");
            break;
        }
        sum->lines += d.lines;
        sum->words += d.words;
        sum->bytes += d.bytes;
        (*records)++;
    }
}

static void reap(void)
{
    int st;
    for (;;) {
        pid_t p = wait(&st);
        if (p == -1) {
            if (errno == EINTR) {
                notice();
                continue;
            }
            break;                      /* ECHILD: потомков больше нет */
        }
        if (WIFEXITED(st))
            printf("родитель: потомок PID %d завершился с кодом %d\n", p, WEXITSTATUS(st));
        else if (WIFSIGNALED(st))
            printf("родитель: потомок PID %d убит сигналом %d\n", p, WTERMSIG(st));
    }
}

int run_procs(int fd, long size)
{
    int pfd[2] = { -1, -1 };
    pid_t pids[16];
    struct Total sum = { 0, 0, 0 };
    long records = 0;
    double start = now();

    install_handlers();
    if (opt.mask)
        set_mask(SIG_BLOCK);
    if (opt.use_pipe && pipe(pfd) == -1) {
        perror("pipe");
        return 1;
    }
    printf("родитель PID %d: потомков %d, файл %ld байт, блок %d\n", getpid(), opt.procs, size,
           opt.block);
    for (int i = 0; i < opt.procs; i++) {
        fflush(stdout);
        pids[i] = fork();
        if (pids[i] == -1) {
            perror("fork");
            return 1;
        }
        if (pids[i] == 0) {
            if (opt.use_pipe)
                close(pfd[0]);
            child(i, fd, size * i / opt.procs, size * (i + 1) / opt.procs, pfd[1]);
        }
        printf("родитель: потомок %d, PID %d\n", i, pids[i]);
    }
    fflush(stdout);
    if (opt.use_pipe) {
        if (!opt.leak)
            close(pfd[1]);              /* свой записывающий конец родителю не нужен */
        read_records(pfd[0], &sum, &records);
        close(pfd[0]);
    }
    if (opt.mask)
        set_mask(SIG_UNBLOCK);
    notice();
    if (stop) {
        printf("родитель: остановлен сигналом %d, потомкам посылается SIGTERM\n", (int)stop);
        for (int i = 0; i < opt.procs; i++)
            kill(pids[i], SIGTERM);
    }
    if (opt.nowait) {
        printf("родитель: wait не вызывается (-n)\n");
        return 0;
    }
    reap();
    if (opt.use_pipe) {
        print_total(stop ? "частичный итог по записям" : "итог по записям канала", &sum);
        printf("записей получено: %ld\n", records);
    } else {
        printf("родитель: итога по файлу не знает — потомки напечатали свои части сами\n");
    }
    printf("SIGCHLD получено: %d (потомков %d)\n", (int)chld_count, opt.procs);
    printf("время %.4f с\n", now() - start);
    return stop ? 2 : 0;
}
