# Makefile for Proxy Lab 
#
# You may modify this file any way you like (except for the handin
# rule). You instructor will type "make" on your specific Makefile to
# build your proxy from sources.

CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -lpthread

all: proxy cache_test
cache.o: cache.c cache.h
	$(CC) $(CFLAGS) -c cache.c

csapp.o: csapp.c csapp.h
	$(CC) $(CFLAGS) -c csapp.c

proxy.o: proxy.c csapp.h
	$(CC) $(CFLAGS) -c proxy.c

cache_test.o: cache_test.c
	$(CC) $(CFLAGS) -c cache_test.c
	
proxy: proxy.o csapp.o cache.o
	$(CC) $(CFLAGS) proxy.o csapp.o cache.o -o proxy $(LDFLAGS)

cache_test: cache_test.o csapp.o cache.o
	$(CC) $(CFLAGS) cache_test.o csapp.o cache.o -o cache_test $(LDFLAGS)
# Creates a tarball in ../proxylab-handin.tar that you can then
# hand in. DO NOT MODIFY THIS!
handin:
	(make clean; cd ..; tar cvf $(USER)-proxylab-handin.tar proxylab-handout --exclude tiny --exclude nop-server.py --exclude proxy --exclude driver.sh --exclude port-for-user.pl --exclude free-port.sh --exclude ".*")

clean:
	rm -f *~ *.o proxy core *.tar *.zip *.gzip *.bzip *.gz

