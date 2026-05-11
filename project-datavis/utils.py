from pathlib import Path
import pandas as pd


def runs_crashes(config_dir: Path) -> tuple[int, int]:
    """Reads a summary.txt for a certain configuration and returns a tuple containing (# runs, # crashes)"""
    df = pd.read_csv(config_dir / "summary.csv")
    num_runs: int = df.shape[0] - 1
    num_crashes: int = (df["status"] == "CRASH").sum()
    return (num_runs, num_crashes)


def form_median_dataframe(config_dir: Path) -> pd.DataFrame | pd.Series:
    summary = pd.read_csv(config_dir / "summary.csv")
    successful_runs = set(summary.loc[summary["status"] == "OK", "run"].astype(str))

    run_fileset = []
    for run_dir in sorted(config_dir.rglob("run_*")):
        if run_dir.is_dir() and run_dir.name.removeprefix("run_") in successful_runs:
            stats_file = run_dir / "stats.csv"
            if stats_file.is_file():
                run_fileset.append(stats_file)

    if not run_fileset:
        return pd.DataFrame(columns=["value", "pct", "cum_pct"])

    df = pd.concat([pd.read_csv(f) for f in run_fileset], ignore_index=True)
    result = df.groupby("stat")[["value", "pct", "cum_pct"]].median()
    return result


def form_mean_dataframe(config_dir: Path) -> pd.DataFrame | pd.Series:
    run_fileset = []
    run_dirs = sorted(config_dir.rglob("run_*"))
    for run_dir in run_dirs:
        if run_dir.is_dir():
            stats_file = run_dir / "stats.csv"
            if stats_file.is_file():
                run_fileset.append(stats_file)

    df = pd.concat([pd.read_csv(f) for f in run_fileset], ignore_index=True)
    result = df.groupby("stat")[["value", "pct", "cum_pct"]].mean()
    return result


def runs_dataframe(config_dir: Path) -> list[pd.DataFrame]:
    run_fileset = []
    run_dirs = sorted(config_dir.rglob("run_*"))
    for run_dir in run_dirs:
        if run_dir.is_dir():
            stats_file = run_dir / "stats.csv"
            if stats_file.is_file():
                run_fileset.append(stats_file)
    run_dfs = []
    for run in run_fileset:
        df = pd.read_csv(run)
        run_dfs.append(df)
    return run_dfs
