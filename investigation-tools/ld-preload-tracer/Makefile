CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -fPIC

all: trace_sigaltstack.so

trace_sigaltstack.so: trace_sigaltstack.c
	$(CC) $(CFLAGS) -shared -o $@ $< -ldl -lpthread

clean:
	rm -f trace_sigaltstack.so

.PHONY: all clean
