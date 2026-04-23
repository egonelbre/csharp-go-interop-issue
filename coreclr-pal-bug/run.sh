#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

echo "Building C library..."
make clean && make

echo "Building .NET project..."
dotnet build -c Release

echo "Running reproducer..."
echo "Expected: SIGSEGV crash within seconds due to sigaltstack overflow"
echo ""

LD_LIBRARY_PATH=. ./bin/Release/net10.0/repro "$@"