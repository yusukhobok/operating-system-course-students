# Сквозная программа РГР wcstat. Одинакова у всех студентов.
# Сборка: make    Очистка: make clean
CC = gcc
CFLAGS = -Wall -g -O0 -pthread
OBJ = wcstat.o procs.o threads.o mem.o ipc.o

wcstat: $(OBJ)
	$(CC) $(CFLAGS) -o wcstat $(OBJ)

%.o: %.c wcstat.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f wcstat *.o

.PHONY: clean
