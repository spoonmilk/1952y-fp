"""
Analysis for CSCI1952Y final project: ECC in L1 Cache Schemes.
Reads from results.csv and produces three plots (real data only).

Usage:
    python3 main.py [--csv PATH] [--output PATH]

    --csv     Path to results CSV  (default: ./output/results.csv)
    --output  Directory for plots  (default: ./output)
"""

import argparse
from pathlib import Path

import pandas as pd

from parse import load_results
from plots import (
    plot_corrections_by_workload,
    plot_completion_vs_prob,
    plot_corrections_vs_prob,
)


def _load_csv(csv_path):
    df = pd.read_csv(csv_path)
    df["completed"] = df["completed"].astype(str).str.strip() == "True"
    df["chaos_prob"] = pd.to_numeric(df["chaos_prob"], errors="coerce")
    df["scrub_interval"] = pd.to_numeric(df["scrub_interval"], errors="coerce")
    return df


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--results",
        default=str(Path(__file__).parent.parent / "gem5" / "results"),
        help="Path to gem5 results directory (used to rebuild CSV if missing)",
    )
    parser.add_argument(
        "--csv",
        default=None,
        help="Path to results CSV (default: <output>/results.csv)",
    )
    parser.add_argument(
        "--output",
        default=str(Path(__file__).parent / "output"),
        help="Directory for plots and CSV",
    )
    args = parser.parse_args()

    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    csv_path = Path(args.csv) if args.csv else output_dir / "results.csv"

    # rebuild CSV from raw stats if it doesn't exist
    if not csv_path.exists():
        import csv as csv_mod
        print(f"CSV not found at {csv_path}, building from {args.results} ...")
        records = load_results(args.results)
        if not records:
            print("ERROR: no results found")
            return
        with open(csv_path, "w", newline="") as f:
            writer = csv_mod.DictWriter(f, fieldnames=list(records[0].keys()))
            writer.writeheader()
            writer.writerows(records)
        print(f"  wrote {len(records)} rows to {csv_path}")

    print(f"Loading {csv_path} ...")
    df = _load_csv(csv_path)
    print(f"  {len(df)} rows, {df['completed'].sum()} completed")

    print("\nChart 1: corrections by workload ...")
    plot_corrections_by_workload(df, output_dir)

    print("Chart 2: completion vs prob (MM) ...")
    plot_completion_vs_prob(df, output_dir)

    print("Chart 3: corrections vs prob (MM, completed) ...")
    plot_corrections_vs_prob(df, output_dir)

    print(f"\nDone. Plots written to {output_dir}/")


if __name__ == "__main__":
    main()
