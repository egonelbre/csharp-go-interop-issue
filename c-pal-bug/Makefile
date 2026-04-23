# Build C library for CoreCLR PAL sigaltstack overflow reproducer

CC := gcc
CFLAGS := -O2 -fPIC -shared
LDFLAGS := -lpthread

# Main C library target
libclib.so: clib.c
	$(CC) $(CFLAGS) -o $@ $<

# Sigaltstack workaround helper
libsigstack_helper.so: sigstack_helper.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Build both libraries
all: libclib.so libsigstack_helper.so

clean:
	rm -f *.so

.PHONY: all clean