from pathlib import Path
import pandas as pd
import altair as alt
import utils

STAT_SOLOMON_SUCCESSFUL_CORRECTIONS = (
    "system.cpu.dcache.solomon.totalSuccessfulCorrections"
)
STAT_HAMMING_SUCCESSFUL_CORRECTIONS = (
    "system.cpu.dcache.hamming.totalSuccessfulCorrections"
)
STAT_SOLOMON_SCRUB_ATTEMPTED_CORRECTIONS = (
    "system.cpu.dcache.solomon.numScrubAttemptedCorrections"
)
STAT_HAMMING_SCRUB_ATTEMPTED_CORRECTIONS = (
    "system.cpu.dcache.hamming.numScrubAttemptedCorrections"
)
STAT_SOLOMON_ACCESS_ATTEMPTED_CORRECTIONS = (
    "system.cpu.dcache.solomon.numAccessAttemptedCorrections"
)
STAT_HAMMING_ACCESS_ATTEMPTED_CORRECTIONS = (
    "system.cpu.dcache.hamming.numAccessAttemptedCorrections"
)
STAT_FAULTS = "system.chaos.numFaultsInjected"

SOLOMON_HYP_DIRECTORY = "./data/solomon/MD/h3"
HAMMING_HYP_DIRECTORY = "./data/hamming/MD/h3"


def create_summary_df(hyp_dir: Path, which_cache: str) -> pd.DataFrame:
    rows = []
    for config_dir in hyp_dir.iterdir():
        if not config_dir.is_dir():
            continue
        median_df = utils.form_median_dataframe(config_dir)
        successful_corrections = 0
        attempted_corrections = 0

        if which_cache == "hamming":
            successful_corrections = (
                median_df.loc[STAT_HAMMING_SUCCESSFUL_CORRECTIONS, "value"]
                if STAT_HAMMING_SUCCESSFUL_CORRECTIONS in median_df.index
                else 0
            )
            attempted_scrub_corrections = (
                median_df.loc[STAT_HAMMING_SCRUB_ATTEMPTED_CORRECTIONS, "value"]
                if STAT_HAMMING_SCRUB_ATTEMPTED_CORRECTIONS in median_df.index
                else 0
            )
            attempted_access_corrections = (
                median_df.loc[STAT_HAMMING_ACCESS_ATTEMPTED_CORRECTIONS, "value"]
                if STAT_HAMMING_ACCESS_ATTEMPTED_CORRECTIONS in median_df.index
                else 0
            )
            attempted_corrections = (
                attempted_scrub_corrections + attempted_access_corrections
            )
        elif which_cache == "solomon":
            successful_corrections = (
                median_df.loc[STAT_SOLOMON_SUCCESSFUL_CORRECTIONS, "value"]
                if STAT_SOLOMON_SUCCESSFUL_CORRECTIONS in median_df.index
                else 0
            )
            attempted_scrub_corrections = (
                median_df.loc[STAT_SOLOMON_SCRUB_ATTEMPTED_CORRECTIONS, "value"]
                if STAT_SOLOMON_SCRUB_ATTEMPTED_CORRECTIONS in median_df.index
                else 0
            )
            attempted_access_corrections = (
                median_df.loc[STAT_SOLOMON_ACCESS_ATTEMPTED_CORRECTIONS, "value"]
                if STAT_SOLOMON_ACCESS_ATTEMPTED_CORRECTIONS in median_df.index
                else 0
            )
            attempted_corrections = (
                attempted_scrub_corrections + attempted_access_corrections
            )
        else:
            attempted_corrections = 0

        faults: int = (
            median_df.loc[STAT_FAULTS, "value"] if STAT_FAULTS in median_df.index else 0
        )
        rows.append(
            {
                "config": config_dir.name,
                "totalSuccessfulCorrections": successful_corrections,
                "attemptedCorrections": attempted_corrections,
                "numFaultsInjected": faults,
            }
        )
    return pd.DataFrame(rows)


def plot_h3():
    solomon_sum = create_summary_df(Path(SOLOMON_HYP_DIRECTORY), "solomon")
    hamming_sum = create_summary_df(Path(HAMMING_HYP_DIRECTORY), "hamming")
    solomon_sum["successful_correction_rate"] = (
        hamming_sum["attemptedCorrections"] / hamming_sum["totalSuccessfulCorrections"]
    )
    hamming_sum["successful_correction_rate"] = (
        hamming_sum["attemptedCorrections"] / hamming_sum["totalSuccessfulCorrections"]
    )

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
                "successful_correction_rate:Q",
                title="Correction Rate (# attempted corrections / # successful corrections)",
            ),
            color=alt.Color("scheme:N", title="Error Correction Method"),
        )
        .properties(title="Successful Error Correction Rate by Configuration")
    )
    h3_chart.save(figures_path, ppi=300)
