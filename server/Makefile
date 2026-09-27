
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Wextra -Werror
LDFLAGS ?= -pthread -lrt

all: aesdsocket
default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) aesdsocket.c -o aesdsocket $(LDFLAGS)

clean:
	rm -f aesdsocket

.PHONY: all default clean
