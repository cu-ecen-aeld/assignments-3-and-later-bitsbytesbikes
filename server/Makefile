CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -Werror -ggdb -pthread 
LFLAGS=-lrt

all: aesdsocket


aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c $(LFLAGS)

clean:
	rm -rf *.o aesdsocket list_t

