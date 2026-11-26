/* ЛР 8. Процесс с несколькими потоками — чтобы рассмотреть их в ps -L, top -H и /proc.
   Запуск: ./threads        — потоки спят 60 с
           ./threads busy   — потоки 30 с крутятся в цикле и занимают процессор
   В конце печатает, сколько прошло времени и сколько процессорного времени потрачено.
   Число потоков — 4. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#define THREADS 4

int seconds;                    /* сколько секунд работает каждый поток */
int busy;                       /* 1 — крутиться в цикле, 0 — спать */

double now(clockid_t clock)     /* показание часов в секундах, с долями */
{
    struct timespec ts;
    clock_gettime(clock, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void *worker(void *arg)
{
    if (!busy) {
        sleep(seconds);
        return NULL;
    }
    double end = now(CLOCK_MONOTONIC) + seconds;
    while (now(CLOCK_MONOTONIC) < end)  /* активная работа: процессор занят всё время */
        ;
    return NULL;
}

int main(int argc, char *argv[])
{
    busy = argc == 2 && strcmp(argv[1], "busy") == 0;
    if (argc > 2 || (argc == 2 && !busy)) {
        fprintf(stderr, "usage: %s [busy]\n", argv[0]);
        return 1;
    }
    seconds = busy ? 30 : 60;
    printf("процесс %d, потоков %d, режим %s, %d с\n",
           getpid(), THREADS, busy ? "busy" : "sleep", seconds);
    fflush(stdout);
    double start = now(CLOCK_MONOTONIC);
    pthread_t t[THREADS];
    for (int i = 0; i < THREADS; i++) {
        int err = pthread_create(&t[i], NULL, worker, NULL);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            return 1;
        }
    }
    for (int i = 0; i < THREADS; i++)
        pthread_join(t[i], NULL);
    printf("все потоки завершились: прошло %.1f с, процессорное время всех потоков %.1f с\n",
           now(CLOCK_MONOTONIC) - start, now(CLOCK_PROCESS_CPUTIME_ID));
    return 0;
}
