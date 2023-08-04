CC = gcc
#CFLAGS = -Wall -Wextra -Werror
CFLAGS = 
INCLUDES = -I.

# Libraries
PTHREAD_FLAGS = -pthread
LRT_FLAGS = -lrt

# Source files
SERVER_SRC = server.c common.h clientqueue.h filequeue.h hashtable.h
CLIENT_SRC = client.c common.h filequeue.h hashtable.h

# Object files
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)

all: server client

server: $(SERVER_OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(INCLUDES) $(LRT_FLAGS) $(PTHREAD_FLAGS)

client: $(CLIENT_OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(INCLUDES) $(LRT_FLAGS) $(PTHREAD_FLAGS)

%.o: %.c
	$(CC) -c $< $(CFLAGS) $(INCLUDES)

clean:
	rm -f *.o server client