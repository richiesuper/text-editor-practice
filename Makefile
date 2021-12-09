CC=cc
CFLAGS=-Wall -Wextra -Wshadow -pedantic -std=c99 -O2
BIN_DIR=bin
SRCS=ted.c
EXECS=$(BIN_DIR)/ted

all: prep $(EXECS)

clean:
	rm -rf $(EXECS)

prep:
	mkdir -p bin

$(EXECS): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

.PHONY:
	all clean prep
