#!/bin/bash

OUTPUT_DIR="$1"
DEFAULT_BINARY="tests/test-progs/isa-assignment/leaky-prog"
BINARY="${2:-$DEFAULT_BINARY}"
TMP_DIR=$(mktemp -d)

# INPUTS=("supersecretdata" "notsosecretdata" "otherdata")
INPUTS=("notsosecretdata" "notsosecretdata" "notsosecretdata")

nRuns=10

declare -A counts
for inp in "${INPUTS[@]}"; do
    counts["$inp"]=0
done

for i in $(seq 1 "$nRuns"); do
    idx=$(( RANDOM % 3 ))
    chosen="${INPUTS[$idx]}"
    counts["$chosen"]=$(( counts["$chosen"] + 1 ))

    /gem5_build/gem5.debug -d "$TMP_DIR" configs/assignments/hw4/o3_bin_args.py --binary "$BINARY" --argv "$chosen"

    mv "$TMP_DIR/stats.txt" "${OUTPUT_DIR}/${chosen}_${counts[$chosen]}.txt"
done

rm -rf "$TMP_DIR"

{
    echo "Input Distribution (${nRuns} runs)"
    for inp in "${INPUTS[@]}"; do
        echo "${inp}: ${counts[$inp]}"
    done
} > "${OUTPUT_DIR}/distribution.log"
