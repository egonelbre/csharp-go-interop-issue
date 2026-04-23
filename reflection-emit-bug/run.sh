#!/usr/bin/env bash
# Reflection.Emit CoreCLR sigaltstack overflow reproducer
# Tests if dynamic code generation creates complex IP boundaries that trigger overflow

set -euo pipefail

cd "$(dirname "$0")"

build() {
    echo "=== building reflection.emit reproducer ==="
    DOTNET_CLI_HOME=/tmp DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 \
        dotnet build -c Release --nologo -v quiet
}

run_once() {
    DOTNET_CLI_HOME=/tmp ./bin/Release/net10.0/repro-reflection-emit "$@"
}

run_loop() {
    local max=${1:-10}
    local attempt rc
    for attempt in $(seq 1 "$max"); do
        echo "--- attempt $attempt ---"
        set +e
        timeout 60 env DOTNET_CLI_HOME=/tmp ./bin/Release/net10.0/repro-reflection-emit
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
    default) build; run_loop 10 ;;
    *)       echo "usage: $0 [build|run|loop [N]]" >&2; exit 2 ;;
esac