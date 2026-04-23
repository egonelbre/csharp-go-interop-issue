#!/usr/bin/env bash
# Multi-Stack C reproducer for CoreCLR sigaltstack overflow
# Artificially replicates Go's enter/exitsyscall conditions

set -euo pipefail

cd "$(dirname "$0")"

build() {
    echo "=== building multi-stack C reproducer ==="
    make clean
    make
}

run_once() {
    ./multistack_reproducer "$@"
}

run_loop() {
    local max=${1:-10}
    local attempt rc
    for attempt in $(seq 1 "$max"); do
        echo "--- attempt $attempt ---"
        set +e
        timeout 60 ./multistack_reproducer
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

case "${1:-default}" in
    build)   build ;;
    run)     run_once ;;
    loop)    run_loop "${2:-10}" ;;
    default) build; run_loop 3 ;;
    *)       echo "usage: $0 [build|run|loop [N]]" >&2; exit 2 ;;
esac