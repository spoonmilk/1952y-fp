import csv
from pathlib import Path


def _parse_stats_file(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("-") or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            name = parts[0]
            values = []
            for part in parts[1:4]:
                try:
                    values.append(float(part.rstrip("%")))
                except ValueError:
                    break
            if not values:
                continue
            while len(values) < 3:
                values.append("")
            rows.append([name] + values)
    return rows


def write_run_csv(run_dir: Path):
    stats_file = run_dir / "stats.txt"
    if not stats_file.exists():
        return

    rows = _parse_stats_file(stats_file)
    if not rows:
        return

    out = run_dir / "stats.csv"
    with open(out, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["stat", "value", "pct", "cum_pct"])
        writer.writerows(rows)


def process_results(results_dir):
    results_dir = Path(results_dir)
    count = 0
    for run_dir in sorted(results_dir.rglob("run_*")):
        if run_dir.is_dir():
            write_run_csv(run_dir)
            count += 1


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("Usage: python stats-to-csv.py <results_dir>")
        sys.exit(1)
    process_results(sys.argv[1])
