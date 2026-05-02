#!/usr/bin/env python3
import csv
import math
import subprocess
import sys
from pathlib import Path

SCRIPTS_DIR = Path(__file__).parent
GEM5_DIR = SCRIPTS_DIR.parent
RESULTS_DIR = GEM5_DIR / "scripts" / "results"

CHAOS_PROB  = 0.001
CHAOS_BITS  = 1
SYM_ERRORS  = 1
NUM_RUNS    = 1

DEFAULT_BENCHES = [
    "MM",
    # "ML2",
    # "ML2_BW_ld",
    # "ML2_BW_ldst",
    # "MC",
    # "MCS",
    # "MI",
    # "MIM",
    # "MIM2",
    # "MD",
    # "STL2",
    # "STc",
]

CPU_STATS = [
    "simTicks",
    "simSeconds",
    "simInsts",
    "system.cpu.cpi",
    "system.cpu.ipc",
    "system.cpu.numCycles",
    "system.cpu.commitStats0.numLoadInsts",
    "system.cpu.commitStats0.numStoreInsts",
]
DCACHE_STATS = [
    "system.cpu.dcache.demandHits",
    "system.cpu.dcache.demandMisses",
    "system.cpu.dcache.demandMissRate",
    "system.cpu.dcache.demandAccesses",
    "system.cpu.dcache.overallHits",
    "system.cpu.dcache.overallMisses",
    "system.cpu.dcache.overallMissRate",
    "system.cpu.dcache.overallAccesses",
    "system.cpu.dcache.writebacks",
]
ICACHE_STATS = [
    "system.cpu.icache.overallHits",
    "system.cpu.icache.overallMisses",
    "system.cpu.icache.overallMissRate",
    "system.cpu.icache.overallAccesses",
]
ECC_STAT_SUFFIXES = [
    "numScrubPasses",
    "numScrubBlocksChecked",
    "numScrubClean",
    "numScrubCorrected",
    "numScrubUnrecoverable",
    "totalScrubCycles",
    "numAccessCorrected",
    "numAccessUnrecoverable",
    "numUnrecoverableDirty",
]


def ecc_keys(cache_type, location):
    prefix = f"system.cpu.{location}.{cache_type}"
    return [f"{prefix}.{s}" for s in ECC_STAT_SUFFIXES]


def parse_stats(path):
    scalars = {}
    totals = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("-"):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            key, raw = parts[0], parts[1]
            try:
                val = float(raw)
            except ValueError:
                continue
            if key.endswith("::total"):
                totals[key[: -len("::total")]] = val
            elif "::" not in key:
                scalars[key] = val
    scalars.update(totals)
    return scalars


def _col(key):
    return key.replace("system.cpu.", "").replace("system.", "")


def build_row(stats, cache_type, bench, run):
    row = {"cache": cache_type, "bench": bench, "run": run}

    for key in CPU_STATS + DCACHE_STATS + ICACHE_STATS + ecc_keys(cache_type, "dcache") + ecc_keys(cache_type, "icache"):
        row[_col(key)] = stats.get(key, math.nan)

    return row


def run_sim(cache_type, bench, run_idx):
    cmd = [
        str(SCRIPTS_DIR / "run-test.sh"),
        cache_type, bench,
        "--chaos-prob", str(CHAOS_PROB),
        "--chaos-bits", str(CHAOS_BITS),
        "--run", str(run_idx),
    ]
    if cache_type == "solomon":
        cmd += ["--sym-errors", str(SYM_ERRORS)]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"    FAILED (exit {result.returncode})", file=sys.stderr)
        out = result.stdout + result.stderr
        if out:
            print(out[:2000], file=sys.stderr)
        return False
    return True


def main():
    cache_types = ["hamming", "solomon"]
    RESULTS_DIR.mkdir(exist_ok=True)

    for cache_type in cache_types:
        rows = []
        for bench in DEFAULT_BENCHES:
            for run_idx in range(1, NUM_RUNS + 1):
                stats_path = (
                    RESULTS_DIR / cache_type / bench / f"run_{run_idx}" / "stats.txt"
                )
                ok = run_sim(cache_type, bench, run_idx)
                print("." if ok else "x", end="", flush=True)
                if not stats_path.exists():
                    continue
                stats = parse_stats(stats_path)
                rows.append(build_row(stats, cache_type, bench, run_idx))
            print()

        if not rows:
            print(f"no results for {cache_type} — all runs failed", file=sys.stderr)
            continue

        csv_path = RESULTS_DIR / f"{cache_type}_results.csv"
        with open(csv_path, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)


if __name__ == "__main__":
    main()
