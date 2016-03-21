CC=gcc
CFLAGS=-Wall -std=c99

new: clean all

all: utils.o melt.o
	$(CC) $(CFLAGS) utils.o melt.o -o melt

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c utils.c

melt.o: melt.c
	$(CC) $(CFLAGS) -c melt.c

clean:
	rm -rf *.o
	rm -rf melt
