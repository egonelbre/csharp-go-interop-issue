# Minimal C Reproducer Build System

CC = gcc
CFLAGS = -O2 -g -Wall -Wextra -std=gnu99 -fPIC
LDFLAGS = -shared -lpthread

C_LIB = libminimal_c_lib.so
C_SOURCE = minimal_reproducer.c
DOTNET_EXE = bin/Release/net10.0/minimal-repro

.PHONY: all build-c build-dotnet clean test

all: build-c build-dotnet

# Build C library
build-c: $(C_LIB)

$(C_LIB): $(C_SOURCE)
	$(CC) $(CFLAGS) $(C_SOURCE) $(LDFLAGS) -o $(C_LIB)

# Build .NET program
build-dotnet: $(C_LIB)
	DOTNET_CLI_HOME=/tmp DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 \
		dotnet build -c Release --nologo -v quiet

# Test runs with different complexity levels
test-minimal: all
	LD_LIBRARY_PATH=. DOTNET_CLI_HOME=/tmp ./$(DOTNET_EXE) 1

test-medium: all
	LD_LIBRARY_PATH=. DOTNET_CLI_HOME=/tmp ./$(DOTNET_EXE) 2

test-high: all
	LD_LIBRARY_PATH=. DOTNET_CLI_HOME=/tmp ./$(DOTNET_EXE) 3

# Clean build artifacts
clean:
	rm -f $(C_LIB)
	rm -rf bin obj

# Debug with GDB
debug: all
	LD_LIBRARY_PATH=. gdb --args ./$(DOTNET_EXE)