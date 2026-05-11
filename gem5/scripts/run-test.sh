#!/usr/bin/env bash
# args: <hamming|solomon> <bench> [--chaos-prob P] [--chaos-bits B]
#                                 [--sym-errors N] [--scrub-interval N]
#                                 [--scrub-tighten-factor F] [--scrub-relax-factor F]
#                                 [--cache-size SIZE]
#                                 [--delay T] [--run N] [--outdir PATH]
# When --outdir is provided, results are written there directly. Otherwise
# the default layout results/<cache>/<bench>[/run_N] is used. The --outdir
# form is what sweeps should use to avoid collisions when running in parallel

set -euo pipefail

GEM5_DIR="$(cd "$(dirname "$0")/.." && pwd)"
GEM5_BIN="/gem5_build/gem5.debug"

CACHE="${1:?Usage: run-test.sh <hamming|solomon> <bench>}"; shift
BENCH="${1:?Usage: run-test.sh <hamming|solomon> <bench>}"; shift

CHAOS_PROB="0.0001" # default is 0.0001 # I ran everything with 0.0001...
CHAOS_BITS="1"
SYM_ERRORS="4"
DELAY="52077000"
SCRUB_INTERVAL="10000000000" # the default
SCRUB_TIGHTEN_FACTOR="1.0"
SCRUB_RELAX_FACTOR="1.0"
CACHE_SIZE="32kB"
RUN=""
OUTDIR_OVERRIDE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --chaos-prob)           CHAOS_PROB="$2";           shift 2 ;;
        --chaos-bits)           CHAOS_BITS="$2";           shift 2 ;;
        --sym-errors)           SYM_ERRORS="$2";           shift 2 ;;
        --delay)                DELAY="$2";                shift 2 ;;
        --scrub-interval)       SCRUB_INTERVAL="$2";       shift 2 ;;
        --scrub-tighten-factor) SCRUB_TIGHTEN_FACTOR="$2"; shift 2 ;;
        --scrub-relax-factor)   SCRUB_RELAX_FACTOR="$2";   shift 2 ;;
        --cache-size)           CACHE_SIZE="$2";           shift 2 ;;
        --run)                  RUN="$2";                  shift 2 ;;
        --outdir)               OUTDIR_OVERRIDE="$2";      shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

case "$CACHE" in
    hamming) CONFIG="$GEM5_DIR/configs/cs1952y-fp/example/example_of_chaos.py" ;;
    solomon) CONFIG="$GEM5_DIR/configs/cs1952y-fp/example/solomon_cache_workload.py" ;;
    *) echo "Did not specify a valid cache"; exit 1 ;;
esac

case "$BENCH" in
    DLP)     BINARY_REL="tests/test-progs/cs1952y-fp/vec.rvv" ;;
    DLP_NP)  BINARY_REL="tests/test-progs/cs1952y-fp/vec_no_parallel" ;;
    *)       BINARY_REL="microbench/$BENCH/bench.RISCV" ;;
esac

[[ -f "$GEM5_DIR/$BINARY_REL" ]] || {
    echo "Workload not found: $GEM5_DIR/$BINARY_REL"
    exit 1
}

if [[ -n "$OUTDIR_OVERRIDE" ]]; then
    OUTDIR="$OUTDIR_OVERRIDE"
else
    OUTDIR="$GEM5_DIR/results/$CACHE/$BENCH${RUN:+/run_$RUN}"
fi
mkdir -p "$OUTDIR"

EXTRA=()
[[ "$CACHE" == "solomon" ]] && EXTRA+=(--symbol-errors "$SYM_ERRORS")

timeout --signal=KILL 15 "$GEM5_BIN" --outdir="$OUTDIR" "$CONFIG" \
    "$BINARY_REL" \
    --chaos-prob "$CHAOS_PROB" \
    --chaos-bits "$CHAOS_BITS" \
    --delay "$DELAY" \
    --scrub-interval "$SCRUB_INTERVAL" \
    --scrub-tighten-factor "$SCRUB_TIGHTEN_FACTOR" \
    --scrub-relax-factor "$SCRUB_RELAX_FACTOR" \
    --cache-size "$CACHE_SIZE" \
    "${EXTRA[@]}"