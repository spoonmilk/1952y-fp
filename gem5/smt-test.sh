#!/bin/bash

OUTPUT_DIR=./test-out/hw5/
TMP_DIR=$(mktemp -d)

# Base (Uniform)
/gem5_build/gem5.debug -d "$TMP_DIR" configs/assignments/hw5/base.py ./tests/test-progs/smt-branches/uniform
mv "$TMP_DIR/stats.txt" "${OUTPUT_DIR}/base-uniform.txt"

# Base (Nonuniform)
/gem5_build/gem5.debug -d "$TMP_DIR" configs/assignments/hw5/base.py ./tests/test-progs/smt-branches/nonuniform
mv "$TMP_DIR/stats.txt" "${OUTPUT_DIR}/base-nonuniform.txt"

# SMT (Both harts Uniform)
/gem5_build/gem5.debug -d "$TMP_DIR" configs/assignments/hw5/smt.py \
    ./tests/test-progs/smt-branches/uniform \
    ./tests/test-progs/smt-branches/uniform
mv "$TMP_DIR/stats.txt" "${OUTPUT_DIR}/smt-2-uniform.txt"

# SMT (Both harts Nonuniform)
/gem5_build/gem5.debug -d "$TMP_DIR" configs/assignments/hw5/smt.py \
    ./tests/test-progs/smt-branches/nonuniform \
    ./tests/test-progs/smt-branches/nonuniform
mv "$TMP_DIR/stats.txt" "${OUTPUT_DIR}/smt-2-nonuniform.txt"

# SMT (U + N)
/gem5_build/gem5.debug -d "$TMP_DIR" configs/assignments/hw5/smt.py \
    ./tests/test-progs/smt-branches/uniform \
    ./tests/test-progs/smt-branches/nonuniform
mv "$TMP_DIR/stats.txt" "${OUTPUT_DIR}/smt-uniform-nonuniform.txt"

rm -rf "$TMP_DIR"
