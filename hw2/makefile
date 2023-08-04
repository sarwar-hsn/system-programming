CC = gcc
CFLAGS = -Wall -Wextra -pedantic
LDFLAGS = 

SRCS = main.c
OBJS = $(SRCS:.c=.o)
TARGET = terminal

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(OBJS) $(TARGET) log* file.txt | clear
