CC=$(CROSS_COMPILE)gcc

all: writer


writer: writer.c
	$(CC) -o writer writer.c
ifneq ($(CC),gcc)
	file writer > ../assignments/assignment2/fileresult.txt
endif


clean:
	rm -rf *.o writer
