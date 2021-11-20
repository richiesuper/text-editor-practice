CC=gcc
CFLAGS=-Wall -Wextra -Wshadow -pedantic -std=c99

seggs: seggs.c
	$(CC) $(CFLAGS) -o $@ $^
