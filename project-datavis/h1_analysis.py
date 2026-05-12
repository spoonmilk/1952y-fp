from pathlib import Path
import pandas as pd
import altair as alt
import utils

STAT_SOLOMON_CORRECTIONS = "system.cpu.dcache.solomon.totalSuccessfulCorrections"
STAT_HAMMING_CORRECTIONS = "system.cpu.dcache.hamming.totalSuccessfulCorrections"
STAT_FAULTS = "system.chaos.numFaultsInjected"

SOLOMON_HYP_DIRECTORY = "./data/solomon/MD/h1"
HAMMING_HYP_DIRECTORY = "./data/hamming/MD/h1"


def create_summary_df(hyp_dir: Path, corrections_stat: str) -> pd.DataFrame:
    rows = []
    for config_dir in hyp_dir.iterdir():
        if not config_dir.is_dir():
            continue
        median_df = utils.form_median_dataframe(config_dir)
        corrections: int = (
            median_df.loc[corrections_stat, "value"]
            if corrections_stat in median_df.index
            else 0
        )
        faults: int = (
            median_df.loc[STAT_FAULTS, "value"] if STAT_FAULTS in median_df.index else 0
        )
        rows.append(
            {
                "config": config_dir.name,
                "totalSuccessfulCorrections": corrections,
                "numFaultsInjected": faults,
            }
        )
    return pd.DataFrame(rows)


def plot_h1():
    solomon_sum = create_summary_df(
        Path(SOLOMON_HYP_DIRECTORY), STAT_SOLOMON_CORRECTIONS
    )
    hamming_sum = create_summary_df(
        Path(HAMMING_HYP_DIRECTORY), STAT_HAMMING_CORRECTIONS
    )
    solomon_sum["correction_rate"] = (
        solomon_sum["totalSuccessfulCorrections"] / solomon_sum["numFaultsInjected"]
    )
    hamming_sum["correction_rate"] = (
        hamming_sum["totalSuccessfulCorrections"] / hamming_sum["numFaultsInjected"]
    )

    solomon_sum["scheme"] = "Solomon"
    hamming_sum["scheme"] = "Hamming"

    combined = pd.concat([solomon_sum, hamming_sum], ignore_index=True)

    figures_path = Path("./figures/h1/h1.svg")
    figures_path.parent.mkdir(parents=True, exist_ok=True)

    h1_chart = (
        alt.Chart(combined)
        .mark_bar()
        .encode(
            x=alt.X("config:N", title="Configuration"),
            xOffset=alt.XOffset("scheme:N").scale(paddingInner=0.1),
            y=alt.Y(
                "correction_rate:Q", title="Correction Rate (# corrections / # faults)"
            ),
            color=alt.Color("scheme:N", title="Error Correction Method"),
            tooltip=[
                "config",
                "scheme",
                "correction_rate",
                "totalSuccessfulCorrections",
                "numFaultsInjected",
            ],
        )
        .properties(title="Error Correction Rate by Configuration")
    )
    h1_chart.save(figures_path)
