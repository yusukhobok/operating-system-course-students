/* ЛР 4. Вывод N строк функцией printf (библиотека C) или системным вызовом write.
   Запуск: ./lines <число строк> printf
           ./lines <число строк> write */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 3 || (strcmp(argv[2], "printf") != 0 && strcmp(argv[2], "write") != 0)) {
        fprintf(stderr, "usage: %s <число строк> printf|write\n", argv[0]);
        return 1;
    }
    int count = atoi(argv[1]);
    char line[64];

    for (int i = 1; i <= count; i++) {
        int len = snprintf(line, sizeof(line), "line %d\n", i);
        if (strcmp(argv[2], "printf") == 0) {
            printf("%s", line);                 /* в буфер библиотеки */
        } else if (write(1, line, len) == -1) { /* сразу ядру */
            perror("write");
            return 1;
        }
    }
    return 0;                                   /* здесь буфер printf сбрасывается */
}
