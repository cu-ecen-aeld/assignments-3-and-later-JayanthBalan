
CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall -Wextra -Werror

all: aesdsocket
default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) aesdsocket.c -o aesdsocket

clean:
	rm -f aesdsocket

.PHONY: all default clean
