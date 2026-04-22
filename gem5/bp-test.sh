#!/bin/bash

OUTPUT_DIR="./test-out/hw5/bp"
BENCH_DIR="./tests/test-progs/microbench-riscv"
TMP_DIR=$(mktemp -d)

BENCHMARKS=("CCa_bench.RISCV" "CCm_bench.RISCV" "CRm_bench.RISCV")
PREDICTORS=("hybrid" "tournament" "local" "bimode" "tage" "perceptron")

for pred in "${PREDICTORS[@]}"; do
    for bench in "${BENCHMARKS[@]}"; do
        bench_name="${bench%.RISCV}"
        echo "Running $pred on $bench_name..."
        /gem5_build/gem5.debug -d "$TMP_DIR" configs/assignments/hw5/bp.py \
            --branchpred "$pred" \
            "$BENCH_DIR/$bench"
        mv "$TMP_DIR/stats.txt" "${OUTPUT_DIR}/${pred}/${bench_name}.txt"
    done
done

rm -rf "$TMP_DIR"
