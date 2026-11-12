/* ЛР 8, 9.
   wcstat -t T [-x]          T потоков считают свои части в общий итог: без блокировки (гонка) или под мьютексом
   wcstat -t T -q Q [-D]     читатель кладёт блоки в очередь из Q мест, T потоков берут и считают;
                             -D — нечётные потоки берут два мьютекса в обратном порядке */
#include "wcstat.h"
#include <pthread.h>
#include <sched.h>

static int fd;
static long size;
static struct Total total;                      /* общий итог */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int next_part;                           /* номер части для очередного потока */

static int take_number(void)
{
    pthread_mutex_lock(&lock);
    int n = next_part++;
    pthread_mutex_unlock(&lock);
    return n;
}

/* ЛР 8: поток считает свою часть файла */
static void *worker(void *arg)
{
    int part = take_number();
    long from = size * part / opt.threads, to = size * (part + 1) / opt.threads, pos = from;
    char *buf = malloc(opt.block);
    int inword = inword_before(fd, from);

    (void)arg;
    if (buf == NULL)
        return NULL;
    while (pos < to && !stop) {
        long want = to - pos < opt.block ? to - pos : opt.block;
        ssize_t n = pread(fd, buf, want, pos);
        if (n <= 0)
            break;
        if (opt.mutex)
            pthread_mutex_lock(&lock);
        count_block(buf, n, &inword, &total);   /* общий итог: все потоки пишут в одни и те же числа */
        if (opt.mutex)
            pthread_mutex_unlock(&lock);
        pos += n;
        pause_block();
    }
    free(buf);
    return NULL;
}

int run_threads(int f, long s)
{
    pthread_t th[16];
    struct Total ref = { 0, 0, 0 };
    double start = now(), spent;

    fd = f;
    size = s;
    printf("потоков %d, блок %d байт, %s\n", opt.threads, opt.block,
           opt.mutex ? "общий итог под мьютексом" : "общий итог без блокировки");
    for (int i = 0; i < opt.threads; i++) {
        if (pthread_create(&th[i], NULL, worker, NULL) != 0) {
            perror("pthread_create");
            return 1;
        }
    }
    for (int i = 0; i < opt.threads; i++)
        pthread_join(th[i], NULL);
    spent = now() - start;
    print_total("итог потоков", &total);
    printf("время счёта потоками %.4f с\n", spent);
    opt.pause_ms = 0;
    count_range(fd, 0, size, &ref);
    print_total("для сравнения (один поток)", &ref);
    printf("потеряно: строк %ld, слов %ld, байт %ld\n", ref.lines - total.lines,
           ref.words - total.words, ref.bytes - total.bytes);
    return 0;
}

/* ЛР 9: очередь блоков */
struct Slot {
    char *data;
    int n;
    int inword;                                 /* был ли предыдущий блок оборван внутри слова */
};

static struct Slot ring[64];
static int head, tail, filled, done;
static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;      /* очередь */
static pthread_mutex_t m_total = PTHREAD_MUTEX_INITIALIZER;    /* общий итог */
static pthread_mutex_t m_log = PTHREAD_MUTEX_INITIALIZER;      /* журнал: число обработанных блоков */
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
static long log_blocks, full_waits, empty_waits;

static void add(const struct Total *d)
{
    total.lines += d->lines;
    total.words += d->words;
    total.bytes += d->bytes;
    log_blocks++;
}

static void *consumer(void *arg)
{
    int me = take_number();
    char *buf = malloc(opt.block);

    (void)arg;
    if (buf == NULL)
        return NULL;
    for (;;) {
        struct Total d = { 0, 0, 0 };
        int n, inword;

        pthread_mutex_lock(&qlock);
        while (filled == 0 && !done) {
            empty_waits++;
            pthread_cond_wait(&not_empty, &qlock);
        }
        if (filled == 0) {                      /* читатель закончил, очередь пуста */
            pthread_mutex_unlock(&qlock);
            break;
        }
        n = ring[head].n;
        inword = ring[head].inword;
        memcpy(buf, ring[head].data, n);
        head = (head + 1) % opt.queue;
        filled--;
        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&qlock);

        count_block(buf, n, &inword, &d);
        pause_block();
        if (opt.deadlock && me % 2 == 1) {
            pthread_mutex_lock(&m_log);         /* обратный порядок: сначала журнал */
            sched_yield();                      /* поток могут вытеснить, пока он держит мьютекс */
            pthread_mutex_lock(&m_total);
        } else {
            pthread_mutex_lock(&m_total);       /* обычный порядок: сначала итог */
            sched_yield();
            pthread_mutex_lock(&m_log);
        }
        add(&d);
        pthread_mutex_unlock(&m_log);
        pthread_mutex_unlock(&m_total);
    }
    free(buf);
    return NULL;
}

int run_queue(int f, long s)
{
    pthread_t th[16];
    char *buf = malloc(opt.block);
    long pos = 0;
    int inword = 0, percent = 0;
    double start = now();

    fd = f;
    size = s;
    if (buf == NULL)
        return 1;
    for (int i = 0; i < opt.queue; i++) {
        ring[i].data = malloc(opt.block);
        if (ring[i].data == NULL)
            return 1;
    }
    printf("потоков %d, очередь %d мест, блок %d байт%s\n", opt.threads, opt.queue, opt.block,
           opt.deadlock ? ", нечётные потоки берут мьютексы в обратном порядке" : "");
    fflush(stdout);
    for (int i = 0; i < opt.threads; i++)
        pthread_create(&th[i], NULL, consumer, NULL);

    while (pos < size) {
        long want = size - pos < opt.block ? size - pos : opt.block;
        ssize_t n = pread(fd, buf, want, pos);
        if (n <= 0)
            break;
        pthread_mutex_lock(&qlock);
        while (filled == opt.queue) {
            full_waits++;
            pthread_cond_wait(&not_full, &qlock);
        }
        memcpy(ring[tail].data, buf, n);
        ring[tail].n = n;
        ring[tail].inword = inword;
        tail = (tail + 1) % opt.queue;
        filled++;
        pthread_cond_signal(&not_empty);
        pthread_mutex_unlock(&qlock);
        inword = !isspace((unsigned char)buf[n - 1]);
        pos += n;
        if (pos * 10 / size > percent) {
            percent = pos * 10 / size;
            printf("прочитано %d%%\n", percent * 10);
            fflush(stdout);
        }
    }
    pthread_mutex_lock(&qlock);
    done = 1;
    pthread_cond_broadcast(&not_empty);
    pthread_mutex_unlock(&qlock);
    for (int i = 0; i < opt.threads; i++)
        pthread_join(th[i], NULL);

    print_total("итог", &total);
    printf("обработано блоков: %ld, время %.4f с\n", log_blocks, now() - start);
    printf("ожиданий: читатель (очередь полна) %ld, потоки (очередь пуста) %ld\n", full_waits,
           empty_waits);
    return 0;
}
