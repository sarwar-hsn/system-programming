CC = gcc
PTHREAD_FLAGS = -pthread
LRT_FLAGS = -lrt

all: server client

server: server.c app.h processpool.h queue.h protocols.h
	gcc server.c -o server -pthread -lrt

client: client.c app.h processpool.h queue.h protocols.h
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f server client
