# Simplified CoreCLR Overflow Reproducer
# Minimal version with 74% code reduction that maintains overflow capability

CC = gcc
CFLAGS = -O2 -g -Wall -Wextra -std=gnu99 -fPIC
LDFLAGS = -shared -lpthread

C_LIB = libcomplex_c_lib.so
C_SOURCES = simplified_c_lib.c simplified_atypical.c
DOTNET_EXE = bin/Release/net10.0/simplified-reproducer

.PHONY: all build-c build-dotnet clean test test-quick

all: build-c build-dotnet

# Build C library with essential atypical calling conventions
build-c: $(C_LIB)

$(C_LIB): $(C_SOURCES)
	$(CC) $(CFLAGS) $(C_SOURCES) $(LDFLAGS) -o $(C_LIB)

# Build .NET host program
build-dotnet: $(C_LIB)
	DOTNET_CLI_HOME=/tmp DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 \
		dotnet build -c Release --nologo -v quiet

# Test with original parameters (may take time to crash)
test: all
	LD_LIBRARY_PATH=. DOTNET_CLI_HOME=/tmp ./$(DOTNET_EXE)

# Quick test with reduced complexity for fast verification
test-quick: all
	REPRO_WORKERS=8 REPRO_ITERATIONS=1000 \
	LD_LIBRARY_PATH=. DOTNET_CLI_HOME=/tmp ./$(DOTNET_EXE)

# Very quick test for immediate verification
test-minimal: all
	REPRO_WORKERS=4 REPRO_ITERATIONS=500 \
	LD_LIBRARY_PATH=. DOTNET_CLI_HOME=/tmp ./$(DOTNET_EXE)

# Clean build artifacts
clean:
	rm -f $(C_LIB)
	rm -rf bin obj

# Debug with GDB
debug: all
	LD_LIBRARY_PATH=. gdb --args ./$(DOTNET_EXE)