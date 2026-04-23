CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -fno-omit-frame-pointer
LDFLAGS ?= -ldl -lpthread

all: host

host: host.c fat_handler.c
	$(CC) $(CFLAGS) -o $@ host.c fat_handler.c $(LDFLAGS)

clean:
	rm -f host

.PHONY: all clean
