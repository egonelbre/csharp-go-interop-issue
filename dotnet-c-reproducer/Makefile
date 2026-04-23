# Build system for .NET → C P/Invoke reproducer

CC = gcc
CFLAGS = -O2 -g -Wall -Wextra -std=gnu99 -fPIC
LDFLAGS = -shared -lpthread

C_LIB = libcomplex_c_lib.so
C_SOURCES = complex_c_lib.c extreme_complexity.c atypical_calling_conventions.c atypical_c_functions.c
DOTNET_EXE = bin/Release/net10.0/repro-dotnet-c

.PHONY: all build-c build-dotnet clean test

all: build-c build-dotnet

# Build C library
build-c: $(C_LIB)

$(C_LIB): $(C_SOURCES)
	$(CC) $(CFLAGS) $(C_SOURCES) $(LDFLAGS) -o $(C_LIB)

# Build .NET program
build-dotnet: $(C_LIB)
	DOTNET_CLI_HOME=/tmp DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 \
		dotnet build -c Release --nologo -v quiet

# Test single run
test: all
	LD_LIBRARY_PATH=. DOTNET_CLI_HOME=/tmp ./$(DOTNET_EXE)

# Clean build artifacts
clean:
	rm -f $(C_LIB)
	rm -rf bin obj

# Debug with GDB
debug: all
	LD_LIBRARY_PATH=. gdb --args ./$(DOTNET_EXE)