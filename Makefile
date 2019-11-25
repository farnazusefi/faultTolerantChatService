CC=gcc
LD=gcc
CFLAGS=-g -Wall -std=c99 -DLOG_USE_COLOR
CPPFLAGS=-I. -I/home/cs417/exercises/ex3/include
SP_LIBRARY=/home/cs417/exercises/ex3/libspread-core.a /home/cs417/exercises/ex3/libspread-util.a

all: client server

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

client:  client.o log.o
	$(LD) -o $@ client.o log.o -ldl $(SP_LIBRARY)

server:  server.o log.o hashset.o
	$(LD) -o $@ server.o log.o hashset.o -ldl $(SP_LIBRARY)


clean:
	rm -f *.o client server

