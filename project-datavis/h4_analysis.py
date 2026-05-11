from pathlib import Path
import pandas as pd
import altair as alt
import utils

STAT_SOLOMON_SCRUB_CORRECTED = "system.cpu.dcache.solomon.numScrubCorrected"
STAT_HAMMING_SCRUB_CORRECTED = "system.cpu.dcache.hamming.numScrubCorrected"

SOLOMON_HYP_VECTORIZED_DIRECTORY = "./data/solomon/DLP/h4"
SOLOMON_HYP_NP_DIRECTORY = "./data/solomon/DLP_NP/h4"
HAMMING_HYP_VECTORIZED_DIRECTORY = "./data/hamming/DLP/h4"
HAMMING_HYP_NP_DIRECTORY = "./data/hamming/DLP_NP/h4"


def create_summary_df(hyp_dir: Path, stat: str) -> pd.DataFrame:
    rows = []
    for config_dir in hyp_dir.iterdir():
        if not config_dir.is_dir():
            continue
        median_df = utils.form_median_dataframe(config_dir)
        value: int = median_df.loc[stat, "value"] if stat in median_df.index else 0
        rows.append(
            {
                "config": config_dir.name,
                "numScrubCorrected": value,
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

    # Add scheme and workload labels
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
        combined.groupby(["scheme", "workload"])["numScrubCorrected"]
        .mean()
        .reset_index()
    )

    figures_path = Path("./figures/h4/h4.svg")
    figures_path.parent.mkdir(parents=True, exist_ok=True)

    h4_chart = (
        alt.Chart(agg_data)
        .mark_bar()
        .encode(
            x=alt.X("scheme:N", title="Error Correction Scheme"),
            xOffset=alt.XOffset("workload:N").scale(paddingInner=0.1),
            y=alt.Y("numScrubCorrected:Q", title="Mean Number of Scrub Corrections"),
            color=alt.Color("workload:N", title="Workload Type"),
            tooltip=["scheme", "workload", "numScrubCorrected"],
        )
        .properties(title="Scrub Corrections by Scheme and Workload")
    )
    h4_chart.save(figures_path)
