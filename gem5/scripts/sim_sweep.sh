#!/usr/bin/env bash
# Full simulation sweep for H1 (multi-bench) and H2 (prob sweep).
#
# H1 results: results/<cache>/<bench>/run_1/    (chaos_prob=0.0001, all benches)
# H2 results: results/<cache>/MM/run_11..18/    (8 chaos_prob levels on MM)
#   - high-prob runs may abort (empty stats.txt) = completion_rate 0
#
# Run from inside the Docker container:
#   bash /home/cs1952y-user/gem5/scripts/sim_sweep.sh

set -uo pipefail

SCRIPTS_DIR="$(cd "$(dirname "$0")" && pwd)"

ok=0; fail=0; total=0

run_one() {
    local cache="$1" bench="$2" prob="$3" run="$4"; shift 4
    ((total++))
    printf '[%s] (%02d) %-42s ... ' "$(date +%T)" "$total" "$cache/$bench prob=$prob run=$run"
    if "$SCRIPTS_DIR/run-test.sh" "$cache" "$bench" \
            --chaos-prob "$prob" --run "$run" "$@" \
            >/dev/null 2>&1; then
        echo "OK"; ((ok++))
    else
        echo "CRASH (empty stats = completion_rate 0)"; ((fail++))
    fi
}

echo "=== H1: multi-bench sweep (prob=0.0001, run=1) ==="
echo "    Expected: 16 runs, all should complete"
H1_BENCHES=(MM MD MI ML2 MC MCS MIM MIP)
for cache in hamming solomon; do
    for bench in "${H1_BENCHES[@]}"; do
        run_one "$cache" "$bench" 0.0001 1
    done
done

echo ""
echo "=== H2: chaos_prob sweep on MM (run=11..18) ==="
echo "    Expected: low probs complete, high probs crash"
PROBS=(1e-6 1e-5 1e-4 5e-4 1e-3 2e-3 5e-3 1e-2)
RUNS=( 11   12   13   14   15   16   17   18 )
for cache in hamming solomon; do
    for i in "${!PROBS[@]}"; do
        run_one "$cache" MM "${PROBS[$i]}" "${RUNS[$i]}"
    done
done

echo ""
echo "=== SWEEP COMPLETE: $ok completed, $fail crashed, $total total ==="
