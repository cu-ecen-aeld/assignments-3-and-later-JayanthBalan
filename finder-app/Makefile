
CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -Wextra -O2 -g

all: writer

writer: writer.o
	$(CC) writer.o -o writer

writer.o: writer.c
	$(CC) $(CFLAGS) -c writer.c -o writer.o

clean:
	rm -f writer writer.o

.PHONY: clean all
