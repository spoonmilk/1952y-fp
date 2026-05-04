"""
Plots for CSCI1952Y ECC final project — real data only.

Three charts:
  1. corrections_by_workload  — H1: scrub vs access corrections across benches
  2. completion_vs_prob       — H2: MM pass/fail by chaos probability
  3. corrections_vs_prob      — H2: scrub corrections captured across prob levels (MM)
"""

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np
from pathlib import Path

_HAMMING_COLOR = "#2166ac"
_SOLOMON_COLOR = "#d6604d"


def _save(fig, path):
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  saved {path}")


# ── Chart 1 ───────────────────────────────────────────────────────────────────

def plot_corrections_by_workload(df, output_dir):
    """
    H1: scrubbing corrects far more errors than on-access correction alone.

    Uses run_1 multi-bench sweep (prob=1e-4, scrub_interval=10, completed only).
    Bars show scrub-corrected; access-corrected is stacked on top.
    A red hatch cap marks runs with at least one unrecoverable error.
    """
    data = df[
        (df["run"] == "run_1") &
        (df["completed"] == True)
    ].copy()

    if data.empty:
        print("  chart 1: no data, skipping")
        return

    benches = sorted(data["bench"].unique())
    x = np.arange(len(benches))
    w = 0.35

    fig, ax = plt.subplots(figsize=(11, 5))

    for offset, cache, color in [(-w/2, "hamming", _HAMMING_COLOR),
                                  ( w/2, "solomon", _SOLOMON_COLOR)]:
        sub = data[data["cache"] == cache].set_index("bench")
        sc = [sub.loc[b, "numScrubCorrected"]   if b in sub.index else 0 for b in benches]
        ac = [sub.loc[b, "numAccessCorrected"]  if b in sub.index else 0 for b in benches]
        ur = [sub.loc[b, "numScrubUnrecoverable"] if b in sub.index else 0 for b in benches]

        ax.bar(x + offset, sc, w, color=color, alpha=0.85, label=f"{cache} scrub-corrected")
        ax.bar(x + offset, ac, w, bottom=sc, color=color, alpha=0.4,
               label=f"{cache} access-corrected")
        # red hatch cap on bars with unrecoverable errors
        for i, u in enumerate(ur):
            if u > 0:
                cap = max(sc[i] + ac[i], 1) * 0.06
                ax.bar(x[i] + offset, cap, w,
                       bottom=sc[i] + ac[i], color="#d73027",
                       hatch="//", label="unrecoverable (≥1)" if i == 0 and offset < 0 else "_")

    ax.set_xticks(x)
    ax.set_xticklabels(benches)
    ax.set_ylabel("Error count")
    ax.set_title(
        "H1 – ECC Corrections by Workload\n"
        "chaos_prob = 1e-4, scrub_interval = 10 cycles",
        fontsize=12
    )
    ax.legend(fontsize=8, ncol=2)
    ax.yaxis.set_major_locator(mticker.MaxNLocator(integer=True))
    ax.grid(axis="y", alpha=0.3)

    fig.tight_layout()
    _save(fig, Path(output_dir) / "corrections_by_workload.png")


# ── Chart 2 ───────────────────────────────────────────────────────────────────

def plot_completion_vs_prob(df, output_dir):
    """
    H2: program completion rate vs. SEU probability for the MM workload.

    Each point is one measured run. Separate markers distinguish
    scrub_interval=10 (filled) from scrub_interval=100 (open).
    """
    mm = df[df["bench"] == "MM"].copy()
    mm = mm[mm["chaos_prob"].notna()]

    if mm.empty:
        print("  chart 2: no MM data, skipping")
        return

    fig, ax = plt.subplots(figsize=(8, 4))

    for cache, color in [("hamming", _HAMMING_COLOR), ("solomon", _SOLOMON_COLOR)]:
        sub = mm[mm["cache"] == cache].sort_values("chaos_prob")

        for interval, marker, fill, label_suffix in [
            (10,  "o", True,  "scrub every 10 cy"),
            (100, "D", False, "scrub every 100 cy"),
        ]:
            rows = sub[sub["scrub_interval"] == interval]
            if rows.empty:
                continue
            xs = rows["chaos_prob"].values
            ys = rows["completed"].astype(int).values
            fc = color if fill else "white"
            ax.scatter(xs, ys, color=color, marker=marker,
                       facecolors=fc, edgecolors=color,
                       s=70, zorder=4,
                       label=f"{cache} ({label_suffix})")

    ax.set_xscale("log")
    ax.set_xlabel("CHAOS bit-flip probability")
    ax.set_ylabel("Completed (1 = yes, 0 = crash)")
    ax.set_yticks([0, 1])
    ax.set_yticklabels(["crash", "completed"])
    ax.set_ylim(-0.3, 1.3)
    ax.set_title("H2 – MM Workload Completion vs. SEU Probability\n(each point = one run)", fontsize=12)
    ax.legend(fontsize=8, ncol=2)
    ax.grid(True, which="both", alpha=0.25)

    fig.tight_layout()
    _save(fig, Path(output_dir) / "completion_vs_prob.png")


# ── Chart 3 ───────────────────────────────────────────────────────────────────

def plot_corrections_vs_prob(df, output_dir):
    """
    H2 complement: how many errors scrubbing caught in MM runs that completed,
    as a function of chaos_prob.

    Shows scrub-corrected (dominant) and access-corrected (usually near zero)
    separately. Runs at scrub_interval=100 are marked with open symbols.
    """
    mm = df[
        (df["bench"] == "MM") &
        (df["completed"] == True) &
        (df["chaos_prob"].notna())
    ].copy()

    if mm.empty:
        print("  chart 3: no completed MM data, skipping")
        return

    fig, ax = plt.subplots(figsize=(8, 4))

    for cache, color in [("hamming", _HAMMING_COLOR), ("solomon", _SOLOMON_COLOR)]:
        sub = mm[mm["cache"] == cache].sort_values("chaos_prob")

        for interval, marker, fill, label_suffix in [
            (10,  "o", True,  "scrub every 10 cy"),
            (100, "D", False, "scrub every 100 cy"),
        ]:
            rows = sub[sub["scrub_interval"] == interval]
            if rows.empty:
                continue
            xs = rows["chaos_prob"].values
            sc = rows["numScrubCorrected"].values
            ac = rows["numAccessCorrected"].values
            fc = color if fill else "white"

            ax.plot(xs, sc, color=color, marker=marker,
                    markerfacecolor=fc, markeredgecolor=color,
                    linestyle="-" if fill else "--",
                    label=f"{cache} scrub-corrected ({label_suffix})")
            if ac.sum() > 0:
                ax.plot(xs, ac, color=color, marker=marker,
                        markerfacecolor=fc, markeredgecolor=color,
                        linestyle=":", alpha=0.5,
                        label=f"{cache} access-corrected ({label_suffix})")

    ax.set_xscale("log")
    ax.set_xlabel("CHAOS bit-flip probability")
    ax.set_ylabel("Errors corrected")
    ax.set_title(
        "H2 – Corrections Captured vs. SEU Probability (MM, completed runs only)",
        fontsize=12
    )
    ax.legend(fontsize=8)
    ax.grid(True, which="both", alpha=0.25)

    fig.tight_layout()
    _save(fig, Path(output_dir) / "corrections_vs_prob.png")
