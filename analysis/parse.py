"""Load gem5 stats.txt files from the results directory tree."""

import math
from pathlib import Path

ECC_FIELDS = [
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

CPU_FIELDS = [
    "simTicks",
    "simSeconds",
    "simInsts",
    "system.cpu.cpi",
    "system.cpu.ipc",
    "system.cpu.numCycles",
]

DCACHE_FIELDS = [
    "system.cpu.dcache.demandMissRate",
    "system.cpu.dcache.demandAccesses",
    "system.cpu.dcache.demandHits",
    "system.cpu.dcache.demandMisses",
]


def _parse_stats_file(path):
    scalars = {}
    totals = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("-") or line.startswith("#"):
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


def _read_config(run_dir):
    """Return (chaos_prob, scrub_interval) from config.ini, or (nan, nan)."""
    config_ini = run_dir / "config.ini"
    chaos_prob = math.nan
    scrub_interval = math.nan
    if not config_ini.exists():
        return chaos_prob, scrub_interval
    for line in config_ini.read_text().splitlines():
        if line.startswith("probability="):
            try:
                chaos_prob = float(line.split("=", 1)[1])
            except ValueError:
                pass
        elif line.startswith("scrub_interval_cycles="):
            try:
                scrub_interval = int(line.split("=", 1)[1])
            except ValueError:
                pass
    return chaos_prob, scrub_interval


def load_results(results_dir):
    """Return a list of record dicts, one per run directory found under results_dir.

    Records with completed=False have empty stats (the simulation crashed); all
    ECC counters are 0 and CPU stats are NaN.  These are used to compute H2
    completion rates.
    """
    results_dir = Path(results_dir)
    records = []

    for cache_type in ("hamming", "solomon"):
        cache_dir = results_dir / cache_type
        if not cache_dir.exists():
            continue

        for bench_dir in sorted(cache_dir.iterdir()):
            if not bench_dir.is_dir():
                continue
            bench = bench_dir.name

            # support both flat stats.txt and run_N/stats.txt layouts
            run_dirs = sorted(bench_dir.glob("run_*"))
            if not run_dirs:
                # flat layout — treat the bench dir itself as the run
                run_dirs = [bench_dir]

            for run_dir in run_dirs:
                stats_file = run_dir / "stats.txt"
                if not stats_file.exists():
                    continue

                run = run_dir.name if run_dir.name.startswith("run_") else "run_1"
                chaos_prob, scrub_interval = _read_config(run_dir)
                completed = stats_file.stat().st_size > 0

                if completed:
                    stats = _parse_stats_file(stats_file)
                else:
                    stats = {}

                prefix = f"system.cpu.dcache.{cache_type}"
                record = {
                    "cache": cache_type,
                    "bench": bench,
                    "run": run,
                    "chaos_prob": chaos_prob,
                    "scrub_interval": scrub_interval,
                    "completed": completed,
                }
                for field in CPU_FIELDS:
                    short = field.replace("system.cpu.", "cpu.")
                    record[short] = stats.get(field, math.nan)
                for field in DCACHE_FIELDS:
                    short = field.replace("system.cpu.dcache.", "dcache.")
                    record[short] = stats.get(field, math.nan)
                for field in ECC_FIELDS:
                    record[field] = stats.get(f"{prefix}.{field}", 0.0)

                records.append(record)

    return records
