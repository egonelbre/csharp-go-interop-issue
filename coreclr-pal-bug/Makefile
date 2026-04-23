CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -fno-omit-frame-pointer -fPIC
LDFLAGS ?= -shared

all: libstack_probe.so

libstack_probe.so: stack_probe.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ stack_probe.c

clean:
	rm -f libstack_probe.so

.PHONY: all clean