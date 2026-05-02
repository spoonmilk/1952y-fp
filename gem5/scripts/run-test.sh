#!/usr/bin/env bash
# args: <hamming|solomon> <bench> [--chaos-prob P] [--chaos-bits B] [--sym-errors N]

set -euo pipefail

GEM5_DIR="$(cd "$(dirname "$0")/.." && pwd)"
GEM5_BIN="/gem5_build/gem5.debug"

CACHE="${1:?Usage: run-test.sh <hamming|solomon> <bench>}"; shift
BENCH="${1:?Usage: run-test.sh <hamming|solomon> <bench>}"; shift

CHAOS_PROB="0.0001"
CHAOS_BITS="1"
SYM_ERRORS="4"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --chaos-prob)  CHAOS_PROB="$2"; shift 2 ;;
        --chaos-bits)  CHAOS_BITS="$2"; shift 2 ;;
        --sym-errors)  SYM_ERRORS="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

case "$CACHE" in
    hamming) CONFIG="$GEM5_DIR/configs/cs1952y-fp/example/example_of_chaos.py" ;;
    solomon) CONFIG="$GEM5_DIR/configs/cs1952y-fp/example/solomon_cache_workload.py" ;;
    *) echo "Did not specify a valid cache"; exit 1 ;;
esac

BINARY_REL="microbench/$BENCH/bench.RISCV"
[[ -f "$GEM5_DIR/$BINARY_REL" ]] || { echo "Run compile-microbench.sh first."; exit 1; }

OUTDIR="$GEM5_DIR/results/$CACHE/$BENCH"
mkdir -p "$OUTDIR"

EXTRA=()
[[ "$CACHE" == "solomon" ]] && EXTRA+=(--symbol-errors "$SYM_ERRORS")

"$GEM5_BIN" --outdir="$OUTDIR" "$CONFIG" \
    --binary "$BINARY_REL" \
    --chaos-prob "$CHAOS_PROB" \
    --chaos-bits "$CHAOS_BITS" \
    "${EXTRA[@]}"
