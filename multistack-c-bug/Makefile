# Multi-Stack C Reproducer Build
CC = gcc
CFLAGS = -O2 -g -Wall -Wextra -std=gnu99
LDFLAGS = -pthread

TARGET = multistack_reproducer
SOURCES = multistack_reproducer.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)

test: $(TARGET)
	./$(TARGET)

debug: $(TARGET)
	gdb --args ./$(TARGET)