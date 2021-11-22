CC=gcc
# WINDOWS_CC=x86_64-w64-mingw32-gcc
# WINDOWS_INCLUDE_DIR=windows-includes
CFLAGS=-Wall -Wextra -Wshadow -pedantic -std=c99 -O2
# WINDOWS_CFLAGS=$(CFLAGS) -L./windows-includes
BIN_DIR=bin
SRCS=ted.c
EXECS=$(BIN_DIR)/ted

all: prep $(EXECS)

clean:
	rm -rf $(EXECS)

prep:
	mkdir -p bin

$(BIN_DIR)/ted: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

# $(BIN_DIR)/ted-windows.exe: ted.c
#	$(WINDOWS_CC) $(WINDOWS_CFLAGS) -o $@ $^

.PHONY:
	all clean prep
