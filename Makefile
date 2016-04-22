CFLAGS=-Wall -O2 -std=c99
DESTDIR=/usr/local
bindir=/bin

new: clean all

all: utils.o docker-melt.o
	$(CC) $(CFLAGS) utils.o docker-melt.o -o docker-melt

utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c utils.c

docker-melt.o: docker-melt.c
	$(CC) $(CFLAGS) -c docker-melt.c

clean:
	rm -rf *.o
	rm -rf docker-melt

.PHONY: install
install: docker-melt
	cp docker-melt $(DESTDIR)$(bindir)/

.PHONY: uninstall
uninstall: docker-melt
	rm $(DESTDIR)$(bindir)/docker-melt
