from pathlib import Path
import pandas as pd
import altair as alt
import utils

STAT_SOLOMON_SCRUB_CORRECTED = "system.cpu.dcache.solomon.numScrubCorrected"
STAT_HAMMING_SCRUB_CORRECTED = "system.cpu.dcache.hamming.numScrubCorrected"

SOLOMON_HYP_VECTORIZED_DIRECTORY = "../gem5/results/experiments/solomon/DLP/h4"
SOLOMON_HYP_NP_DIRECTORY = "../gem5/results/experiments/solomon/DLP_NP/h4"
HAMMING_HYP_VECTORIZED_DIRECTORY = "../gem5/results/experiments/hamming/DLP/h4"
HAMMING_HYP_NP_DIRECTORY = "../gem5/results/experiments/hamming/DLP_NP/h4"


def _get_stat(run_df: pd.DataFrame, stat_name: str) -> float:
    """Look up a stat's value in a single run's stats.csv dataframe."""
    match = run_df.loc[run_df["stat"] == stat_name, "value"]
    return float(match.iloc[0]) if not match.empty else 0.0


def create_summary_df(hyp_dir: Path, stat: str) -> pd.DataFrame:
    rows = []
    for config_dir in hyp_dir.iterdir():
        if not config_dir.is_dir():
            continue

        run_dfs = utils.runs_dataframe(config_dir)
        per_run_values = [_get_stat(run_df, stat) for run_df in run_dfs]

        if not per_run_values:
            continue

        rows.append(
            {
                "config": config_dir.name,
                "numScrubCorrected": pd.Series(per_run_values).mean(),
                "num_runs_used": len(per_run_values),
            }
        )
    return pd.DataFrame(rows)


def plot_h4():
    solomon_vec = create_summary_df(
        Path(SOLOMON_HYP_VECTORIZED_DIRECTORY), STAT_SOLOMON_SCRUB_CORRECTED
    )
    solomon_np = create_summary_df(
        Path(SOLOMON_HYP_NP_DIRECTORY), STAT_SOLOMON_SCRUB_CORRECTED
    )
    hamming_vec = create_summary_df(
        Path(HAMMING_HYP_VECTORIZED_DIRECTORY), STAT_HAMMING_SCRUB_CORRECTED
    )
    hamming_np = create_summary_df(
        Path(HAMMING_HYP_NP_DIRECTORY), STAT_HAMMING_SCRUB_CORRECTED
    )

    solomon_vec["scheme"] = "Solomon"
    solomon_vec["workload"] = "Vectorized"

    solomon_np["scheme"] = "Solomon"
    solomon_np["workload"] = "Non-Parallel"

    hamming_vec["scheme"] = "Hamming"
    hamming_vec["workload"] = "Vectorized"

    hamming_np["scheme"] = "Hamming"
    hamming_np["workload"] = "Non-Parallel"

    combined = pd.concat(
        [solomon_vec, solomon_np, hamming_vec, hamming_np], ignore_index=True
    )

    agg_data = (
        combined.groupby(["workload", "scheme"])["numScrubCorrected"]
        .mean()
        .reset_index()
    )

    figures_path = Path("./figures/h4/h4.svg")
    figures_path.parent.mkdir(parents=True, exist_ok=True)

    h4_chart = (
        alt.Chart(agg_data)
        .mark_bar()
        .encode(
            x=alt.X("workload:N", title="Workload Type"),
            xOffset=alt.XOffset("scheme:N").scale(paddingInner=0.1),
            y=alt.Y("numScrubCorrected:Q", title="Mean Number of Scrub Corrections"),
            color=alt.Color("scheme:N", title="Error Correction Scheme"),
            tooltip=["workload", "scheme", "numScrubCorrected"],
        )
        .properties(title="Scrub Corrections by Workload and Scheme")
    )
    h4_chart.save(figures_path)