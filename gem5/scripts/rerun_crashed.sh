#!/usr/bin/env bash
# Rerun workloads that crashed in sim_sweep.sh, using prob=0.0001 and
# scrub_interval=100 to avoid pointer-corruption crashes on MM.
#
# Results go to run_101 (H1 rerun) so they don't overwrite the originals.

set -uo pipefail

SCRIPTS_DIR="$(cd "$(dirname "$0")" && pwd)"

ok=0; fail=0; total=0

run_one() {
    local cache="$1" bench="$2" prob="$3" run="$4" interval="$5"
    ((total++))
    printf '[%s] (%02d) %-42s scrub=%s ... ' \
        "$(date +%T)" "$total" "$cache/$bench prob=$prob run=$run" "$interval"
    if "$SCRIPTS_DIR/run-test.sh" "$cache" "$bench" \
            --chaos-prob "$prob" --scrub-interval "$interval" --run "$run" \
            >/dev/null 2>&1; then
        echo "OK"; ((ok++))
    else
        echo "CRASH"; ((fail++))
    fi
}

echo "=== Rerunning crashed workloads (prob=0.0001, scrub_interval=100) ==="

# H1: MM crashed for hamming at scrub_interval=10; solomon completed but
# run_1 config.ini shows old prob — rerun both cleanly at run_101.
run_one hamming MM 0.0001 101 100
run_one solomon MM 0.0001 101 100

echo ""
echo "=== DONE: $ok completed, $fail crashed, $total total ==="
