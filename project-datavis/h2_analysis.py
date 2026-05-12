import re
from pathlib import Path
import pandas as pd
import altair as alt
import utils

SOLOMON_HYP_DIRECTORY = "./data/solomon/MD/h2"
HAMMING_HYP_DIRECTORY = "./data/hamming/MD/h2"

_CONFIG_TYPE_RE = re.compile(r"^(dynamic|low_scrub|on-access)")


def retrieve_probability(dir: Path) -> float:
    dprob = dir.name.split("_")[-1][1:]
    return float(dprob)


def retrieve_config_type(dir: Path) -> str:
    match = _CONFIG_TYPE_RE.match(dir.name)
    return match.group(1) if match else dir.name


def create_summary_df(hyp_dir: Path) -> pd.DataFrame:
    rows = []
    for config_dir in hyp_dir.iterdir():
        if not config_dir.is_dir():
            continue
        probability: float = retrieve_probability(config_dir)
        config_type: str = retrieve_config_type(config_dir)
        runs, crashes = utils.runs_crashes(config_dir)
        rows.append(
            {
                "configType": config_type,
                "faultProbability": probability,
                "numRuns": runs,
                "numCrashes": crashes,
            }
        )
    return pd.DataFrame(rows)


def plot_h2():
    solomon_sum = create_summary_df(Path(SOLOMON_HYP_DIRECTORY))
    solomon_sum["scheme"] = "Solomon"
    hamming_sum = create_summary_df(Path(HAMMING_HYP_DIRECTORY))
    hamming_sum["scheme"] = "Hamming"
    combined = pd.concat([solomon_sum, hamming_sum], ignore_index=True)

    figure_path = Path("./figures/h2/h2.png")
    figure_path.parent.mkdir(parents=True, exist_ok=True)

    chart = (
        alt.Chart(combined)
        .mark_line(point=True)
        .encode(
            x=alt.X("faultProbability:O", title="Fault Probability"),
            y=alt.Y("numCrashes:Q", title="Crashes"),
            color=alt.Color("configType:N", title="Config Type"),
        )
        .facet(column=alt.Column("scheme:N", title="ECC Scheme"))
    )
    chart.save(figure_path, ppi=300)
