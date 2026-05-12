from pathlib import Path
import pandas as pd
import altair as alt
import utils

STAT_SOLOMON_CORRECTIONS = "system.cpu.dcache.solomon.totalSuccessfulCorrections"
STAT_HAMMING_CORRECTIONS = "system.cpu.dcache.hamming.totalSuccessfulCorrections"
STAT_FAULTS = "system.chaos.numFaultsInjected"

SOLOMON_HYP_DIRECTORY = "../gem5/results/experiments/solomon/MD/h1"
HAMMING_HYP_DIRECTORY = "../gem5/results/experiments/hamming/MD/h1"


def _get_stat(run_df: pd.DataFrame, stat_name: str) -> float:
    """Look up a stat's value in a single run's stats.csv dataframe."""
    match = run_df.loc[run_df["stat"] == stat_name, "value"]
    return float(match.iloc[0]) if not match.empty else 0.0


def create_summary_df(hyp_dir: Path, corrections_stat: str) -> pd.DataFrame:
    rows = []
    for config_dir in hyp_dir.iterdir():
        if not config_dir.is_dir():
            continue

        run_dfs = utils.runs_dataframe(config_dir)

        per_run_ratios = []
        per_run_corrections = []
        per_run_faults = []
        for run_df in run_dfs:
            corrections = _get_stat(run_df, corrections_stat)
            faults = _get_stat(run_df, STAT_FAULTS)
            if faults > 0:
                per_run_ratios.append((faults - corrections) / faults)
                per_run_corrections.append(corrections)
                per_run_faults.append(faults)

        if not per_run_ratios:
            continue

        try:
            num_runs, num_crashes = utils.runs_crashes(config_dir)
        except FileNotFoundError:
            num_runs, num_crashes = len(run_dfs), 0

        ratios_series = pd.Series(per_run_ratios)
        rows.append(
            {
                "config": config_dir.name,
                "uncorrected_ratio": ratios_series.median(),
                "median_corrections": pd.Series(per_run_corrections).median(),
                "median_faults": pd.Series(per_run_faults).median(),
                "num_runs_used": len(per_run_ratios),
                "total_runs": num_runs,
                "num_crashes": num_crashes,
            }
        )
    return pd.DataFrame(rows)


def _print_crash_report(scheme: str, df: pd.DataFrame):
    print(f"\n=== {scheme} crashes per configuration ===")
    if df.empty:
        print("  (no configurations found)")
        return
    for _, row in df.iterrows():
        print(
            f"  {row['config']}: {row['num_crashes']} crashes "
            f"out of {row['total_runs']} runs "
            f"({row['num_runs_used']} used in ratio)"
        )


def plot_h1():
    solomon_sum = create_summary_df(
        Path(SOLOMON_HYP_DIRECTORY), STAT_SOLOMON_CORRECTIONS
    )
    hamming_sum = create_summary_df(
        Path(HAMMING_HYP_DIRECTORY), STAT_HAMMING_CORRECTIONS
    )

    _print_crash_report("Solomon", solomon_sum)
    _print_crash_report("Hamming", hamming_sum)

    solomon_sum["scheme"] = "Solomon"
    hamming_sum["scheme"] = "Hamming"

    combined = pd.concat([solomon_sum, hamming_sum], ignore_index=True)

    figures_dir = Path("./figures/h1")
    figures_dir.mkdir(parents=True, exist_ok=True)

    h1_chart = (
        alt.Chart(combined)
        .mark_bar()
        .encode(
            x=alt.X("config:N", title="Configuration"),
            xOffset=alt.XOffset("scheme:N").scale(paddingInner=0.1),
            y=alt.Y(
                "uncorrected_ratio:Q",
                title="Median Uncorrected Fault Ratio per run ((faults - corrections) / faults)",
            ),
            color=alt.Color("scheme:N", title="Error Correction Method"),
            tooltip=[
                "config",
                "scheme",
                "uncorrected_ratio",
                "median_corrections",
                "median_faults",
                "num_runs_used",
                "total_runs",
                "num_crashes",
            ],
        )
        .properties(title="Uncorrected Fault Ratio by Configuration (per-run median)")
    )
    h1_chart.save(figures_dir / "h1.png")

    crashes_chart = (
        alt.Chart(combined)
        .mark_bar()
        .encode(
            x=alt.X("config:N", title="Configuration"),
            xOffset=alt.XOffset("scheme:N").scale(paddingInner=0.1),
            y=alt.Y("num_crashes:Q", title="Number of Crashes"),
            color=alt.Color("scheme:N", title="Error Correction Method"),
            tooltip=[
                "config",
                "scheme",
                "num_crashes",
                "total_runs",
            ],
        )
        .properties(title="Crash Counts by Configuration")
    )
    crashes_chart.save(figures_dir / "h1_crashes.png")