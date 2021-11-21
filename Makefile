CC=gcc
CFLAGS=-Wall -Wextra -Wshadow -pedantic -std=c99

ted: ted.c
	$(CC) $(CFLAGS) -o $@ $^
