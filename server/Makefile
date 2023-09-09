CC?=$(CROSS_COMPILE)gcc
CFLAGS?=-Wall -Werror -ggdb
LDFLAGS?=-lrt -pthread

all: aesdsocket


aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c $(LFLAGS)

clean:
	rm -rf *.o aesdsocket list_t

