#Cross-compiling check
CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -Werror -O0

all: aesdsocket
default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

#clean target
clean:
	rm -f aesdsocket *.o

.PHONY: all default clean
