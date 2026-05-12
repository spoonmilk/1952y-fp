from pathlib import Path
import pandas as pd
import altair as alt
import utils

STAT_SOLOMON_SCRUB_ATTEMPTED_CORRECTIONS = (
    "system.cpu.dcache.solomon.numScrubAttemptedCorrections"
)
STAT_HAMMING_SCRUB_ATTEMPTED_CORRECTIONS = (
    "system.cpu.dcache.hamming.numScrubAttemptedCorrections"
)

SOLOMON_HYP_DIRECTORY = "../gem5/results/experiments/solomon/MD/h3"
HAMMING_HYP_DIRECTORY = "../gem5/results/experiments/hamming/MD/h3"


def _get_stat(run_df: pd.DataFrame, stat_name: str) -> float:
    """Look up a stat's value in a single run's stats.csv dataframe."""
    match = run_df.loc[run_df["stat"] == stat_name, "value"]
    return float(match.iloc[0]) if not match.empty else 0.0


def create_summary_df(hyp_dir: Path, which_cache: str) -> pd.DataFrame:
    if which_cache == "hamming":
        scrub_stat = STAT_HAMMING_SCRUB_ATTEMPTED_CORRECTIONS
    elif which_cache == "solomon":
        scrub_stat = STAT_SOLOMON_SCRUB_ATTEMPTED_CORRECTIONS
    else:
        raise ValueError(f"Unknown cache type: {which_cache}")

    rows = []
    for config_dir in hyp_dir.iterdir():
        if not config_dir.is_dir():
            continue

        run_dfs = utils.runs_dataframe(config_dir)

        per_run_scrubs = [_get_stat(run_df, scrub_stat) for run_df in run_dfs]

        if not per_run_scrubs:
            continue

        rows.append(
            {
                "config": config_dir.name,
                "median_scrub_attempted": pd.Series(per_run_scrubs).median(),
                "num_runs_used": len(per_run_scrubs),
            }
        )
    return pd.DataFrame(rows)


def plot_h3():
    solomon_sum = create_summary_df(Path(SOLOMON_HYP_DIRECTORY), "solomon")
    hamming_sum = create_summary_df(Path(HAMMING_HYP_DIRECTORY), "hamming")

    solomon_sum["scheme"] = "Solomon"
    hamming_sum["scheme"] = "Hamming"

    combined = pd.concat([solomon_sum, hamming_sum], ignore_index=True)

    figures_path = Path("./figures/h3/h3.png")
    figures_path.parent.mkdir(parents=True, exist_ok=True)

    h3_chart = (
        alt.Chart(combined)
        .mark_bar()
        .encode(
            x=alt.X("config:N", title="Configuration"),
            xOffset=alt.XOffset("scheme:N").scale(paddingInner=0.1),
            y=alt.Y(
                "median_scrub_attempted:Q",
                title="Median Scrub Attempted Corrections",
            ),
            color=alt.Color("scheme:N", title="Error Correction Method"),
            tooltip=[
                "config",
                "scheme",
                "median_scrub_attempted",
                "num_runs_used",
            ],
        )
        .properties(title="Scrub Attempted Corrections by Configuration (per-run median)")
    )
    h3_chart.save(figures_path, ppi=300)
