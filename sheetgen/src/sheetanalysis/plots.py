"""Required figures for the fuzzing analysis.

    uv run sheet-plots TRIALS_DIR --out FIGURES_DIR

Every figure is produced from the covtrace CSVs alone, so graders can rerun
this script on submitted data.
"""

from __future__ import annotations

import argparse
import itertools
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import Final

import matplotlib.pyplot as plt
import polars as pl
from matplotlib.axes import Axes
from matplotlib.figure import Figure

from sheetanalysis.load import STAGES, edges_on_grid, load_trials

# Categorical slots, assigned in a fixed order by configuration name so a
# configuration keeps its colour across figures and submissions.
_SERIES: Final = ("#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300")
_CONFIG_ORDER: Final = ("B0", "B1", "G0", "G1", "X")
_BAR: Final = "#2a78d6"
_NEUTRAL: Final = "#9a9893"
_TEXT: Final = "#2b2b29"
_MUTED: Final = "#6b6a66"
_GRID: Final = "#e4e3df"
_GRID_POINTS: Final = 200
_LABEL_GAP: Final = 0.05  # minimum gap between end labels, as a share of the y range
_STAGE_LABELS: Final = {"-": "crash or hang", "empty": "empty sheet"}


def _colour(config: str, configs: list[str]) -> str:
    """The configuration's fixed slot: its place in _CONFIG_ORDER, then any
    other configurations alphabetically after those five."""
    extra = sorted(c for c in configs if c not in _CONFIG_ORDER)
    slot = _CONFIG_ORDER.index(config) if config in _CONFIG_ORDER else 5 + extra.index(config)
    return _SERIES[slot % len(_SERIES)]


def _spread(positions: list[float], gap: float) -> list[float]:
    """Moves label positions apart so neighbours are at least `gap` apart."""
    order = sorted(range(len(positions)), key=lambda i: positions[i])
    placed = list(positions)
    for previous, current in itertools.pairwise(order):
        placed[current] = max(placed[current], placed[previous] + gap)
    return placed


def _ordered_configs(trials: pl.DataFrame) -> list[str]:
    present = set(trials["config"].unique().to_list())
    known = [c for c in _CONFIG_ORDER if c in present]
    return known + sorted(present - set(known))


def _style(ax: Axes) -> None:
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(_MUTED)
    ax.tick_params(colors=_MUTED, labelsize=9)
    ax.grid(axis="y", color=_GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def plot_coverage_over_time(trials: pl.DataFrame) -> Figure:
    """Median edges over time per configuration, with a min-max band."""
    end = float(max(trials["time_s"].to_list(), default=0.0))
    grid = [end * i / (_GRID_POINTS - 1) for i in range(_GRID_POINTS)]
    sampled = edges_on_grid(trials, grid)
    summary = (
        sampled.group_by("config", "time_s")
        .agg(
            pl.col("edges").median().alias("median"),
            pl.col("edges").min().alias("low"),
            pl.col("edges").max().alias("high"),
            pl.col("trial").n_unique().alias("trials"),
        )
        .sort("config", "time_s")
    )

    figure, ax = plt.subplots(figsize=(8, 4.5))
    _style(ax)
    configs = _ordered_configs(trials)
    ends: list[tuple[str, float, float]] = []  # (config, x, y) of each median's last point
    for config in configs:
        rows = summary.filter(pl.col("config") == config)
        minutes = [t / 60 for t in rows["time_s"].to_list()]
        colour = _colour(config, configs)
        trial_count = int(max(rows["trials"].to_list(), default=0))
        ax.fill_between(
            minutes,
            rows["low"].to_list(),
            rows["high"].to_list(),
            color=colour,
            alpha=0.15,
            linewidth=0,
        )
        ax.plot(
            minutes,
            rows["median"].to_list(),
            color=colour,
            linewidth=2,
            label=f"{config} (n={trial_count})",
        )
        ends.append((config, minutes[-1], float(rows["median"].to_list()[-1])))
    ax.set_ylim(bottom=0)
    top = ax.get_ylim()[1]
    placed = _spread([y for _, _, y in ends], _LABEL_GAP * top)
    nudge = 0.01 * (ax.get_xlim()[1] - ax.get_xlim()[0])
    for (config, x, _), label_y in zip(ends, placed, strict=True):
        ax.text(x + nudge, label_y, config, va="center", fontsize=9, color=_TEXT)
    ax.set_xlabel("Time (minutes)", color=_TEXT)
    ax.set_ylabel("Edges covered", color=_TEXT)
    ax.set_title(
        "Coverage over time: median of trials, band from min to max",
        loc="left",
        color=_TEXT,
        fontsize=11,
    )
    ax.legend(frameon=False, fontsize=9, loc="lower right")
    figure.tight_layout()
    return figure


def plot_stage_shares(trials: pl.DataFrame) -> Figure:
    """For each configuration, the share of inputs stopping at each stage."""
    configs = _ordered_configs(trials)
    stages = [s for s in STAGES if s in set(trials["stage"].unique().to_list())]
    counts = trials.group_by("config", "stage").len()
    figure, axes = plt.subplots(
        len(configs), 1, figsize=(7, 1.0 + 1.6 * len(configs)), sharex=True, squeeze=False
    )
    for ax, config in zip(axes[:, 0], configs, strict=True):
        rows = counts.filter(pl.col("config") == config)
        total = int(rows["len"].sum())
        by_stage = dict(zip(rows["stage"].to_list(), rows["len"].to_list(), strict=True))
        shares = [100 * by_stage.get(stage, 0) / total if total else 0 for stage in stages]
        colours = [_NEUTRAL if s in ("empty", "-") else _BAR for s in stages]
        _style(ax)
        ax.grid(axis="y", visible=False)
        ax.grid(axis="x", color=_GRID, linewidth=0.8)
        ax.barh([_STAGE_LABELS.get(s, s) for s in stages], shares, color=colours, height=0.7)
        ax.invert_yaxis()
        for y, share in enumerate(shares):
            if share > 0:
                ax.text(share + 1, y, f"{share:.0f}%", va="center", fontsize=8, color=_TEXT)
        ax.set_title(f"{config}  ({total} corpus inputs)", loc="left", fontsize=10, color=_TEXT)
        ax.set_xlim(0, 110)
    axes[-1, 0].set_xlabel("Share of corpus inputs stopping at the stage (%)", color=_TEXT)
    figure.suptitle(
        "Where inputs stop in the pipeline (inputs kept because they added coverage)",
        x=0.02,
        ha="left",
        color=_TEXT,
    )
    figure.tight_layout()
    return figure


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="sheet-plots", description=__doc__)
    parser.add_argument("trials", type=Path, help="directory containing CONFIG/trial-K/")
    parser.add_argument("--out", type=Path, default=Path("figures"))
    args = parser.parse_args(argv)

    trials = load_trials(args.trials)
    args.out.mkdir(parents=True, exist_ok=True)
    figures = {
        "coverage_over_time": plot_coverage_over_time(trials),
        "stage_shares": plot_stage_shares(trials),
    }
    for name, figure in figures.items():
        figure.savefig(args.out / f"{name}.pdf", bbox_inches="tight")
        figure.savefig(args.out / f"{name}.png", dpi=150, bbox_inches="tight")
        plt.close(figure)
    return 0


if __name__ == "__main__":
    sys.exit(main())
