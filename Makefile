CC=gcc
CFLAGS=-Wall -Wextra -Wshadow -pedantic -std=c99

kilo: kilo.c
	$(CC) $(CFLAGS) -o $@ $^
