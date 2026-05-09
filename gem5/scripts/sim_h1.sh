#!/usr/bin/env bash
# Hypothesis 1 sweep: how does scrub policy affect ECC behavior on MD?
#
# Layout written:
#   results/experiments/<cache>/MD/h1/<differentiator>/run_N/   (gem5 outdir)
#   results/experiments/<cache>/MD/h1/<differentiator>/summary.csv
#
# summary.csv columns:
#   run,status,exit_code,wall_seconds,start_iso,end_iso
#
# Crashes don't abort the sweep — they're logged with status=CRASH and we
# move on. stats.txt may still be empty (or absent), which the parser
# already treats as completed=False.
#
# Usage:
#   bash sim_h1.sh                 # both caches
#   bash sim_h1.sh --cache hamming # one cache only
#   bash sim_h1.sh --runs 5        # override run count

# We deliberately do NOT use `set -e` — run-test.sh failures must not abort
# the sweep. We also avoid `((var++))` because in bash that returns exit 1
# when var was 0, which interferes with `if [[ $? ... ]]` checks on the
# preceding command. Use `var=$(( var + 1 ))` instead.
set -uo pipefail

SCRIPTS_DIR="$(cd "$(dirname "$0")" && pwd)"
GEM5_DIR="$(cd "$SCRIPTS_DIR/.." && pwd)"
RUN_TEST="$SCRIPTS_DIR/run-test.sh"

[[ -x "$RUN_TEST" ]] || { echo "ERROR: $RUN_TEST not found or not executable"; exit 1; }

# ---------------- defaults & arg parsing ----------------
HYPOTHESIS="h1"
WORKLOAD="MD"
RUNS=10
CACHES=(hamming solomon)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cache)    CACHES=("$2"); shift 2 ;;
        --runs)     RUNS="$2";     shift 2 ;;
        --workload) WORKLOAD="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ---------------- candidates ----------------
# Each entry: <differentiator>|<extra args to run-test.sh>
CANDIDATES=(
    "on-access|--scrub-interval 0"
    "low_scrub_10000000|--scrub-interval 10000000"
    "high_scrub_10000|--scrub-interval 10000"
    "dynamic_t2.0_r1.25|--scrub-tighten-factor 2.0 --scrub-relax-factor 1.25"
)

total=0
ok=0
fail=0

# ---------------- helpers ----------------
run_one() {
    local cache="$1" diff="$2" run="$3"; shift 3
    local extra=("$@")

    local outdir_base="$GEM5_DIR/results/experiments/$cache/$WORKLOAD/$HYPOTHESIS/$diff"
    local summary="$outdir_base/summary.csv"
    local logdir="$outdir_base/logs"
    mkdir -p "$logdir"

    local default_run_dir="$GEM5_DIR/results/$cache/$WORKLOAD/run_$run"
    local final_run_dir="$outdir_base/run_$run"

    rm -rf "$default_run_dir" "$final_run_dir"

    local start_iso end_iso start_s end_s wall_s status exit_code
    start_iso="$(date -Iseconds)"
    start_s="$(date +%s)"

    "$RUN_TEST" "$cache" "$WORKLOAD" --run "$run" "${extra[@]}" \
        > "$logdir/run_${run}.stdout" 2> "$logdir/run_${run}.stderr"
    exit_code=$?

    end_s="$(date +%s)"
    end_iso="$(date -Iseconds)"
    wall_s=$(( end_s - start_s ))

    if [[ $exit_code -eq 0 ]]; then
        status="OK"
    else
        status="CRASH"
    fi

    if [[ -d "$default_run_dir" ]]; then
        mkdir -p "$outdir_base"
        mv "$default_run_dir" "$final_run_dir"
        rmdir "$GEM5_DIR/results/$cache/$WORKLOAD" 2>/dev/null || true
        rmdir "$GEM5_DIR/results/$cache" 2>/dev/null || true
    fi

    if [[ ! -f "$summary" ]]; then
        echo "run,status,exit_code,wall_seconds,start_iso,end_iso" > "$summary"
    fi
    echo "$run,$status,$exit_code,$wall_s,$start_iso,$end_iso" >> "$summary"

    printf '  run %2d: %-5s (exit=%d, %ds)\n' "$run" "$status" "$exit_code" "$wall_s"

    return $exit_code
}

# ---------------- main loop ----------------
sweep_start="$(date +%s)"

for cache in "${CACHES[@]}"; do
    for entry in "${CANDIDATES[@]}"; do
        diff="${entry%%|*}"
        extra_str="${entry#*|}"
        # shellcheck disable=SC2206
        extra=($extra_str)

        echo ""
        echo "=== $cache / $WORKLOAD / $HYPOTHESIS / $diff ==="
        echo "    extra args: ${extra[*]}"

        for run in $(seq 1 "$RUNS"); do
            total=$(( total + 1 ))
            if run_one "$cache" "$diff" "$run" "${extra[@]}"; then
                ok=$(( ok + 1 ))
            else
                fail=$(( fail + 1 ))
            fi
        done
    done
done

sweep_end="$(date +%s)"
echo ""
echo "=== H1 SWEEP COMPLETE ==="
printf '    %d runs total, %d ok, %d crashed, in %ds\n' \
    "$total" "$ok" "$fail" "$(( sweep_end - sweep_start ))"
printf '    summaries under results/experiments/<cache>/%s/%s/<diff>/summary.csv\n' \
    "$WORKLOAD" "$HYPOTHESIS"