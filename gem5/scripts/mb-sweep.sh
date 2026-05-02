#!/usr/bin/env bash
# args: <hamming|solomon> [--benches b1 b2 ...] [--chaos-prob P] [--chaos-bits B] [--sym-errors N]
# Results go to results/<cache>/<bench>/

set -euo pipefail

SCRIPTS_DIR="$(cd "$(dirname "$0")" && pwd)"
GEM5_DIR="$SCRIPTS_DIR/.."

CACHE="${1:?Usage: sweep.sh <hamming|solomon>}"; shift

BENCHES=()
PASSTHROUGH=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --benches)
            shift
            while [[ $# -gt 0 && "$1" != --* ]]; do BENCHES+=("$1"); shift; done
            ;;
        --chaos-prob|--chaos-bits|--sym-errors)
            PASSTHROUGH+=("$1" "$2"); shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [[ ${#BENCHES[@]} -eq 0 ]]; then
    while IFS= read -r -d '' bin; do
        BENCHES+=("$(basename "$(dirname "$bin")")")
    done < <(find "$GEM5_DIR/microbench" -name "bench.RISCV" -print0 | sort -z)
fi

[[ ${#BENCHES[@]} -eq 0 ]] && { echo "Run compile-microbench.sh first."; exit 1; }

for BENCH in "${BENCHES[@]}"; do
    echo "=== $BENCH ==="
    "$SCRIPTS_DIR/run-test.sh" "$CACHE" "$BENCH" "${PASSTHROUGH[@]}" || echo "gem5 error on $BENCH (exit $?)"
done
