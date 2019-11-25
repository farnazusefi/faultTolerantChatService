CC=gcc
LD=gcc
CFLAGS=-g -Wall -c99
CPPFLAGS=-I. -I/home/cs417/exercises/ex3/include
SP_LIBRARY=/home/cs417/exercises/ex3/libspread-core.a /home/cs417/exercises/ex3/libspread-util.a

all: client server

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

client:  client.o
	$(LD) -o $@ client.o -ldl $(SP_LIBRARY)

server:  server.o
	$(LD) -o $@ server.o -ldl $(SP_LIBRARY)


clean:
	rm -f *.o client server

