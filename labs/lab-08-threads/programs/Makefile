CC = gcc
CFLAGS = -Wall -pthread

all: threads

threads: threads.c
	$(CC) $(CFLAGS) -o threads threads.c

clean:
	rm -f threads *.txt

.PHONY: all clean
