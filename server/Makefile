
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Wextra -Werror
LDFLAGS ?= -pthread -lrt

USE_AESD_CHAR_DEVICE ?= 1
CFLAGS += -DUSE_AESD_CHAR_DEVICE=$(USE_AESD_CHAR_DEVICE)

all: aesdsocket
default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) aesdsocket.c -o aesdsocket $(LDFLAGS)

clean:
	rm -f aesdsocket

.PHONY: all default clean
