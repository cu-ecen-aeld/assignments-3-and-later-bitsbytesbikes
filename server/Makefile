CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -Werror -ggdb

all: aesdsocket


aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

clean:
	rm -rf *.o aesdsocket
