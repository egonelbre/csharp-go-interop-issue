#!/usr/bin/env bash
# Build and run the .NET + Go cgo sigaltstack-race reproducer.
#
# Usage:
#   ./run.sh                  Build once, run until it crashes (max 10 attempts).
#   ./run.sh build            Build only.
#   ./run.sh run              Run once (assumes already built).
#   ./run.sh gc               Run in GC-pressure mode (no synthetic signals —
#                             let CoreCLR's own GC fire the activation signal).
#   ./run.sh gdb              Build, then run under gdb and dump a core at crash.
#   ./run.sh loop [N]         Run N times in a row (default 10) and report rc.
#
# Requirements: Go (1.25+), .NET SDK (10.0+), gcc, gdb (for `gdb` mode).
set -euo pipefail

cd "$(dirname "$0")"

DOTNET_BIN="./bin/Release/net10.0/repro-dotnet"

build() {
    echo "=== building libgolib.so (Go c-shared) ==="
    CGO_ENABLED=1 go build -buildmode=c-shared -o libgolib.so golib.go
    echo "=== building .NET host ==="
    DOTNET_CLI_HOME=/tmp DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 \
        dotnet build -c Release --nologo -v quiet
}

run_once() {
    LD_LIBRARY_PATH=. DOTNET_CLI_HOME=/tmp "$DOTNET_BIN" "$@"
}

run_loop() {
    local max=${1:-10}
    local attempt rc
    for attempt in $(seq 1 "$max"); do
        echo "--- attempt $attempt ---"
        set +e
        timeout 60 env LD_LIBRARY_PATH=. DOTNET_CLI_HOME=/tmp "$DOTNET_BIN"
        rc=$?
        set -e
        echo "exit=$rc"
        if [[ $rc -eq 139 ]]; then
            echo "=== SIGSEGV on attempt $attempt — reproduced ==="
            return 0
        fi
    done
    echo "=== no crash in $max attempts ==="
    return 1
}

run_gdb() {
    mkdir -p ./crash
    gdb -batch -nx \
        -ex 'set pagination off' \
        -ex 'handle all nostop noprint pass' \
        -ex 'handle SIGSEGV stop print' \
        -ex 'run' \
        -ex 'printf "\n===== CRASHED =====\n"' \
        -ex 'thread' \
        -ex 'info registers rip rsp rbp' \
        -ex 'x/4i $rip' \
        -ex 'bt 20' \
        -ex 'gcore ./crash/core' \
        -ex 'info proc mappings' \
        -ex 'thread apply all bt 6' \
        -ex 'quit' \
        --args env LD_LIBRARY_PATH=. DOTNET_CLI_HOME=/tmp "$DOTNET_BIN" \
        2>&1 | tee ./crash/gdb.log
    echo
    echo "core: ./crash/core  (re-analyse with: gdb $DOTNET_BIN ./crash/core)"
}

cmd=${1:-default}
case "$cmd" in
    build)   build ;;
    run)     run_once ;;
    gc)      build; REPRO_MODE=gc run_loop "${2:-10}" ;;
    loop)    run_loop "${2:-10}" ;;
    gdb)     build; run_gdb ;;
    default) build; run_loop 10 ;;
    *)       echo "usage: $0 [build|run|gc [N]|loop [N]|gdb]" >&2; exit 2 ;;
esac
