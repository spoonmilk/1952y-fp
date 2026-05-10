#!/usr/bin/env bash
# Hypothesis 2 sweep: how do scrub policies hold up as SEU probability rises?
#
# Design: 3 candidates × 6 probabilities × 10 runs × 2 caches = 360 jobs.
#
# Layout written:
#   results/experiments/<cache>/MD/h2/<candidate>_p<prob>/run_N/
#   results/experiments/<cache>/MD/h2/<candidate>_p<prob>/summary.csv
#   results/experiments/<cache>/MD/h2/<candidate>_p<prob>/logs/run_N.{stdout,stderr}
#
# The differentiator encodes BOTH the scrub policy AND the chaos probability,
# e.g. "dynamic_t2.0_r1.25_p1e-3". One summary.csv per cell.
#
# Status classification, parallelism, lock-protected summary writes, and
# crash handling all match sim_h1.sh.
#
# Usage:
#   bash sim_h2.sh                 # full sweep, 6 jobs in parallel
#   bash sim_h2.sh --cache hamming # one cache only
#   bash sim_h2.sh --runs 5        # fewer runs per cell
#   bash sim_h2.sh --jobs 4        # less parallelism
#   bash sim_h2.sh --jobs 1        # serial (debugging)

set -uo pipefail

SCRIPTS_DIR="$(cd "$(dirname "$0")" && pwd)"
GEM5_DIR="$(cd "$SCRIPTS_DIR/.." && pwd)"
RUN_TEST="$SCRIPTS_DIR/run-test.sh"

[[ -x "$RUN_TEST" ]] || { echo "ERROR: $RUN_TEST not found or not executable"; exit 1; }

# defaults & arg parsing 
HYPOTHESIS="h2"
WORKLOAD="MD"
RUNS=10
JOBS=6
CACHES=(hamming solomon)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cache)    CACHES=("$2"); shift 2 ;;
        --runs)     RUNS="$2";     shift 2 ;;
        --jobs)     JOBS="$2";     shift 2 ;;
        --workload) WORKLOAD="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# candidates and probabilities
# Candidates: same as H1 minus high_scrub_10000 (the short-interval one).
# Each entry: <candidate_name>|<extra args (excluding --chaos-prob)>
CANDIDATES=(
    "on-access|--scrub-interval 0"
    "low_scrub_10000000|--scrub-interval 10000000"
    "dynamic_t2.0_r1.25|--scrub-tighten-factor 2.0 --scrub-relax-factor 1.25"
)

# Probabilities, sweep by powers of 10
PROBS=(
    "0.000001"
    "0.00001"
    "0.0001"
    "0.001"
    "0.01"
    "0.1"
)
# path-friendly aliases for the same probabilities
PROB_TAGS=(
    "1e-6"
    "1e-5"
    "1e-4"
    "1e-3"
    "1e-2"
    "1e-1"
)

# helpers

# classify_run: same logic as sim_h1.sh
classify_run() {
    local exit_code="$1" run_dir="$2" stderr_log="$3"

    if [[ $exit_code -ne 0 ]]; then
        echo "CRASH|exit_$exit_code"
        return
    fi

    local stats="$run_dir/stats.txt"
    if [[ ! -s "$stats" ]]; then
        echo "CRASH|no_stats"
        return
    fi

    if [[ -s "$stderr_log" ]]; then
        if grep -qE 'Unrecoverable error in dirty block' "$stderr_log"; then
            echo "CRASH|unrecoverable_dirty"
            return
        fi
        if grep -qE 'Verification failure after ECC correction' "$stderr_log"; then
            echo "CRASH|ecc_verify_fail"
            return
        fi
        if grep -qiE '^panic:' "$stderr_log"; then
            echo "CRASH|gem5_panic"
            return
        fi
        if grep -qiE '^fatal:' "$stderr_log"; then
            echo "CRASH|gem5_fatal"
            return
        fi
    fi

    echo "OK|-"
}

# run_one, execute one (cache, candidate, prob, run) job
run_one() {
    local cache="$1" diff="$2" prob="$3" run="$4"; shift 4
    local extra=("$@")

    local outdir_base="$GEM5_DIR/results/experiments/$cache/$WORKLOAD/$HYPOTHESIS/$diff"
    local summary="$outdir_base/summary.csv"
    local lockfile="$outdir_base/.summary.lock"
    local logdir="$outdir_base/logs"
    local final_run_dir="$outdir_base/run_$run"
    local stderr_log="$logdir/run_${run}.stderr"

    mkdir -p "$logdir" "$final_run_dir"

    local start_iso end_iso start_s end_s wall_s exit_code status reason
    start_iso="$(date -Iseconds)"
    start_s="$(date +%s)"

    "$RUN_TEST" "$cache" "$WORKLOAD" \
        --run "$run" \
        --outdir "$final_run_dir" \
        --chaos-prob "$prob" \
        "${extra[@]}" \
        > "$logdir/run_${run}.stdout" 2> "$stderr_log"
    exit_code=$?

    end_s="$(date +%s)"
    end_iso="$(date -Iseconds)"
    wall_s=$(( end_s - start_s ))

    local classification
    classification="$(classify_run "$exit_code" "$final_run_dir" "$stderr_log")"
    status="${classification%%|*}"
    reason="${classification##*|}"

    (
        flock -x 200
        echo "$run,$status,$exit_code,$wall_s,$reason,$start_iso,$end_iso" >> "$summary"
    ) 200>"$lockfile"

    printf '  [%s] %s/%s run %2d: %-5s (exit=%d, %ds, %s)\n' \
        "$(date +%T)" "$cache" "$diff" "$run" "$status" "$exit_code" "$wall_s" "$reason"

    return $exit_code
}

# pre-sweep cleanup
# Truncate (or create) summary.csv for every cell this sweep will touch.
for cache in "${CACHES[@]}"; do
    for entry in "${CANDIDATES[@]}"; do
        candidate="${entry%%|*}"
        for tag in "${PROB_TAGS[@]}"; do
            diff="${candidate}_p${tag}"
            outdir_base="$GEM5_DIR/results/experiments/$cache/$WORKLOAD/$HYPOTHESIS/$diff"
            mkdir -p "$outdir_base"
            echo "run,status,exit_code,wall_seconds,fail_reason,start_iso,end_iso" \
                > "$outdir_base/summary.csv"
            rm -f "$outdir_base/.summary.lock"
        done
    done
done

# build the job list
declare -a JOBS_LIST=()
for cache in "${CACHES[@]}"; do
    for entry in "${CANDIDATES[@]}"; do
        candidate="${entry%%|*}"
        extra_str="${entry#*|}"
        for i in "${!PROBS[@]}"; do
            prob="${PROBS[$i]}"
            tag="${PROB_TAGS[$i]}"
            diff="${candidate}_p${tag}"
            for run in $(seq 1 "$RUNS"); do
                # job format: cache|diff|prob|run|extra_str
                JOBS_LIST+=("$cache|$diff|$prob|$run|$extra_str")
            done
        done
    done
done

total_jobs=${#JOBS_LIST[@]}

# ---------------- pre-flight summary ----------------
echo "=== H2 sweep starting ==="
echo "    caches:       ${CACHES[*]}"
echo "    workload:     $WORKLOAD"
echo "    runs / cell:  $RUNS"
echo "    total jobs:   $total_jobs"
echo "    parallel:     $JOBS"
echo ""
echo "    candidates:"
for entry in "${CANDIDATES[@]}"; do
    candidate="${entry%%|*}"
    extra_str="${entry#*|}"
    printf '      %-22s %s\n' "$candidate" "$extra_str"
done
echo ""
echo "    probabilities (${#PROBS[@]}):"
for i in "${!PROBS[@]}"; do
    printf '      %-10s -> path tag %s\n' "${PROBS[$i]}" "${PROB_TAGS[$i]}"
done
echo ""

# job pool
sweep_start="$(date +%s)"
declare -A INFLIGHT=()

launch_job() {
    local job="$1"
    local cache="${job%%|*}"; job="${job#*|}"
    local diff="${job%%|*}";  job="${job#*|}"
    local prob="${job%%|*}";  job="${job#*|}"
    local run="${job%%|*}";   job="${job#*|}"
    local extra_str="$job"
    # shellcheck disable=SC2206
    local extra=($extra_str)

    run_one "$cache" "$diff" "$prob" "$run" "${extra[@]}" &
    local pid=$!
    INFLIGHT[$pid]="$cache/$diff/run_$run"
}

reap_one() {
    wait -n 2>/dev/null || true
    for pid in "${!INFLIGHT[@]}"; do
        if ! kill -0 "$pid" 2>/dev/null; then
            unset 'INFLIGHT[$pid]'
        fi
    done
}

for job in "${JOBS_LIST[@]}"; do
    while [[ ${#INFLIGHT[@]} -ge $JOBS ]]; do
        reap_one
    done
    launch_job "$job"
done

while [[ ${#INFLIGHT[@]} -gt 0 ]]; do
    reap_one
done

sweep_end="$(date +%s)"

# final tally
ok=0
fail=0
declare -A REASON_COUNT=()
for cache in "${CACHES[@]}"; do
    for entry in "${CANDIDATES[@]}"; do
        candidate="${entry%%|*}"
        for tag in "${PROB_TAGS[@]}"; do
            diff="${candidate}_p${tag}"
            summary="$GEM5_DIR/results/experiments/$cache/$WORKLOAD/$HYPOTHESIS/$diff/summary.csv"
            [[ -f "$summary" ]] || continue
            while IFS=, read -r _ status _ _ reason _ _; do
                case "$status" in
                    OK)    ok=$(( ok + 1 )) ;;
                    CRASH)
                        fail=$(( fail + 1 ))
                        REASON_COUNT[$reason]=$(( ${REASON_COUNT[$reason]:-0} + 1 ))
                        ;;
                esac
            done < <(tail -n +2 "$summary")
        done
    done
done

echo ""
echo "=== H2 SWEEP COMPLETE ==="
printf '    %d jobs total, %d ok, %d crashed, in %ds wall (parallelism=%d)\n' \
    "$total_jobs" "$ok" "$fail" \
    "$(( sweep_end - sweep_start ))" \
    "$JOBS"
if [[ $fail -gt 0 ]]; then
    echo "    crash breakdown:"
    for reason in "${!REASON_COUNT[@]}"; do
        printf '      %-22s %d\n' "$reason" "${REASON_COUNT[$reason]}"
    done
fi
printf '    summaries under results/experiments/<cache>/%s/%s/<diff>/summary.csv\n' \
    "$WORKLOAD" "$HYPOTHESIS"