/* wcstat — основной режим и общие функции. Запуск:
     ./wcstat [-b байт] [-s мс] [-h сек] файл      один процесс читает файл блоками и считает
   Остальные режимы — в procs.c, threads.c, mem.c, ipc.c (см. wcstat.h и rgr/programs/README.md). */
#include "wcstat.h"
#include <sys/stat.h>

struct Options opt = { .block = 4096, .rec = 64, .key = 0x57430000 };
volatile sig_atomic_t stop;

/* Считает строки, слова и байты в одном блоке. *inword — был ли предыдущий символ внутри слова. */
void count_block(const char *buf, int n, int *inword, struct Total *t)
{
    for (int i = 0; i < n; i++) {
        int space = isspace((unsigned char)buf[i]);
        if (buf[i] == '\n')
            t->lines++;
        if (!space && !*inword)
            t->words++;
        *inword = !space;
    }
    t->bytes += n;
}

/* Был ли символ перед позицией pos внутри слова: нужно, чтобы слово на границе частей не посчитали дважды. */
int inword_before(int fd, long pos)
{
    char c;
    if (pos == 0 || pread(fd, &c, 1, pos - 1) != 1)
        return 0;
    return !isspace((unsigned char)c);
}

double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void pause_block(void)
{
    if (opt.pause_ms > 0)
        usleep(opt.pause_ms * 1000);
}

/* Ждёт seconds секунд, отвечая на сигналы; досрочно возвращается, если пришёл сигнал остановки.
   sleep здесь не годится: после прерывания сигналом он возвращает остаток, округлённый вниз. */
void hold(int seconds)
{
    double end = now() + seconds;

    while (!stop && now() < end) {
        usleep(50000);
        notice();
    }
}

void print_total(const char *what, const struct Total *t)
{
    printf("%s: строк %ld, слов %ld, байт %ld\n", what, t->lines, t->words, t->bytes);
}

/* Считает часть файла [from, to) блоками по opt.block байт. Читает pread: позиция передаётся
   в вызове, поэтому процессы и потоки не мешают друг другу. */
int count_range(int fd, long from, long to, struct Total *t)
{
    char *buf = malloc(opt.block);
    int inword = inword_before(fd, from);
    long pos = from;

    if (buf == NULL) {
        perror("malloc");
        return -1;
    }
    while (pos < to && !stop) {
        long want = to - pos < opt.block ? to - pos : opt.block;
        ssize_t n = pread(fd, buf, want, pos);
        if (n < 0) {
            perror(opt.file);
            free(buf);
            return -1;
        }
        if (n == 0)
            break;
        count_block(buf, n, &inword, t);
        pos += n;
        pause_block();
    }
    free(buf);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: wcstat [-b байт] [-s мс] [-h сек] файл\n"
        "  ЛР 5-7:  -p K [-n] [-c [-r байт] [-d сек] [-l]] [-m]\n"
        "  ЛР 8-9:  -t T [-x]  |  -t T -q Q [-D]\n"
        "  ЛР 10:   -M all|map [-w]\n"
        "  ЛР 11:   -S [-k ключ]  |  -a файл [-k ключ]\n");
}

static int number(const char *s, int min, int max, const char *what)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < min || v > max) {
        fprintf(stderr, "wcstat: %s — число от %d до %d\n", what, min, max);
        exit(1);
    }
    return (int)v;
}

int main(int argc, char *argv[])
{
    int c;

    while ((c = getopt(argc, argv, "b:s:h:p:ncr:d:lmt:xq:DM:wSa:k:")) != -1) {
        switch (c) {
        case 'b': opt.block = number(optarg, 1, 1048576, "-b"); break;
        case 's': opt.pause_ms = number(optarg, 0, 10000, "-s"); break;
        case 'h': opt.hold = number(optarg, 0, 3600, "-h"); break;
        case 'p': opt.procs = number(optarg, 1, 16, "-p"); break;
        case 'n': opt.nowait = 1; break;
        case 'c': opt.use_pipe = 1; break;
        case 'r': opt.rec = number(optarg, 32, 4096, "-r"); break;
        case 'd': opt.delay = number(optarg, 0, 600, "-d"); break;
        case 'l': opt.leak = 1; break;
        case 'm': opt.mask = 1; break;
        case 't': opt.threads = number(optarg, 1, 16, "-t"); break;
        case 'x': opt.mutex = 1; break;
        case 'q': opt.queue = number(optarg, 1, 64, "-q"); break;
        case 'D': opt.deadlock = 1; break;
        case 'M':
            if (strcmp(optarg, "all") == 0)
                opt.mem = 1;
            else if (strcmp(optarg, "map") == 0)
                opt.mem = 2;
            else {
                usage();
                return 1;
            }
            break;
        case 'w': opt.cow = 1; break;
        case 'S': opt.serve = 1; break;
        case 'a': opt.ask = optarg; break;
        case 'k': opt.key = strtol(optarg, NULL, 0); break;
        default: usage(); return 1;
        }
    }
    if (opt.serve)
        return run_server();
    if (opt.ask != NULL)
        return run_client();
    if (optind != argc - 1) {
        usage();
        return 1;
    }
    opt.file = argv[optind];

    int fd = open(opt.file, O_RDONLY);
    struct stat st;
    if (fd == -1 || fstat(fd, &st) == -1) {
        perror(opt.file);
        return 1;
    }
    long size = st.st_size;
    int rc = 0;

    if (opt.threads > 0 && opt.queue > 0) {
        rc = run_queue(fd, size);
    } else if (opt.threads > 0) {
        rc = run_threads(fd, size);
    } else if (opt.procs > 0) {
        rc = run_procs(fd, size);
    } else if (opt.mem > 0) {
        rc = run_mem(fd, size);
    } else {
        struct Total t = { 0, 0, 0 };
        double start = now();
        if (count_range(fd, 0, size, &t) == -1)
            return 1;
        print_total(opt.file, &t);
        printf("блоков по %d байт: %ld, время %.4f с\n", opt.block, (size + opt.block - 1) / opt.block,
               now() - start);
    }
    if (!(opt.mem > 0 && opt.cow))
        hold(opt.hold);
    return rc;
}
