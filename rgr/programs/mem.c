/* ЛР 10. wcstat -M all файл   весь файл читается в память (malloc), затем считается
          wcstat -M map файл   файл отображается в память (mmap), страницы читаются при обращении
          -w                   после подсчёта создаётся потомок, который пишет в каждую страницу */
#include "wcstat.h"
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>

/* Печатает, сколько памяти у этого процесса сейчас: VmSize, VmRSS (status), Pss (smaps), отказы страниц. */
static void report(const char *when)
{
    char line[256];
    long vmsize = 0, vmrss = 0, pss = 0, v;
    struct rusage ru;
    FILE *f = fopen("/proc/self/status", "r");

    while (f != NULL && fgets(line, sizeof line, f) != NULL) {
        if (sscanf(line, "VmSize: %ld", &v) == 1)
            vmsize = v;
        if (sscanf(line, "VmRSS: %ld", &v) == 1)
            vmrss = v;
    }
    if (f != NULL)
        fclose(f);
    f = fopen("/proc/self/smaps", "r");
    while (f != NULL && fgets(line, sizeof line, f) != NULL) {
        if (sscanf(line, "Pss: %ld", &v) == 1)
            pss += v;
    }
    if (f != NULL)
        fclose(f);
    getrusage(RUSAGE_SELF, &ru);
    printf("PID %d, %s: VmSize %ld кБ, VmRSS %ld кБ, Pss %ld кБ, отказов страниц %ld\n", getpid(),
           when, vmsize, vmrss, pss, ru.ru_minflt);
    fflush(stdout);
}

/* Потомок делит страницы с родителем, пока не пишет в них; запись копирует страницу (COW). */
static void cow_phase(char *data, long size)
{
    pid_t pid;

    fflush(stdout);
    pid = fork();
    if (pid == -1) {
        perror("fork");
        return;
    }
    if (pid == 0) {
        report("потомок до записи");
        hold(opt.hold);
        for (long i = 0; i < size; i += 4096)
            data[i]++;
        report("потомок после записи");
        hold(opt.hold);
        _exit(0);
    }
    report("родитель после fork");
    hold(2 * opt.hold);
    report("родитель в конце");
    wait(NULL);
}

int run_mem(int fd, long size)
{
    char *data;
    struct Total t = { 0, 0, 0 };
    int inword = 0;
    double start = now();

    report("до загрузки");
    if (opt.mem == 1) {
        long pos = 0;
        data = malloc(size);
        if (data == NULL) {
            perror("malloc");
            return 1;
        }
        while (pos < size) {
            long want = size - pos < opt.block ? size - pos : opt.block;
            ssize_t n = pread(fd, data + pos, want, pos);
            if (n <= 0) {
                perror(opt.file);
                return 1;
            }
            pos += n;
        }
    } else {
        data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) {
            perror("mmap");
            return 1;
        }
    }
    report(opt.mem == 1 ? "файл прочитан в malloc" : "файл отображён mmap");
    for (long pos = 0; pos < size; pos += opt.block) {
        long n = size - pos < opt.block ? size - pos : opt.block;
        count_block(data + pos, n, &inword, &t);
        pause_block();
    }
    report("после подсчёта");
    print_total(opt.file, &t);
    printf("время %.4f с\n", now() - start);
    if (opt.cow)
        cow_phase(data, size);
    return 0;
}
