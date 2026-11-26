"""Loading covtrace output.

run-trials writes one CSV per trial at TRIALS/CONFIG/trial-K/covtrace.csv,
with the columns time_s, inputs, edges, stage, status, file.
"""

from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path
from typing import Final

import polars as pl

SCHEMA: Final = {
    "time_s": pl.Float64,
    "inputs": pl.Int64,
    "edges": pl.Int64,
    "stage": pl.String,
    "status": pl.String,
    "file": pl.String,
}

STAGES: Final = (
    "header",
    "lex",
    "parse",
    "resolve",
    "dependencies",
    "types",
    "eval",
    "done",
    "empty",
    "-",
)


def load_trace(path: Path) -> pl.DataFrame:
    """Loads one covtrace CSV."""
    return pl.read_csv(path, schema=SCHEMA)


def load_trials(root: Path) -> pl.DataFrame:
    """Loads every TRIALS/CONFIG/trial-K/covtrace.csv under `root`.

    Adds `config` (the configuration name) and `trial` (K) columns.
    """
    frames = []
    for path in sorted(root.glob("*/trial-*/covtrace.csv")):
        trial = int(path.parent.name.removeprefix("trial-"))
        frames.append(
            load_trace(path).with_columns(
                pl.lit(path.parent.parent.name).alias("config"),
                pl.lit(trial).alias("trial"),
            )
        )
    if not frames:
        raise FileNotFoundError(f"no */trial-*/covtrace.csv files under {root}")
    return pl.concat(frames)


def edges_on_grid(trials: pl.DataFrame, grid: Sequence[float]) -> pl.DataFrame:
    """Resamples cumulative edge counts onto a shared time grid.

    Coverage is a step function: at time t, a trial's coverage is the edge
    count of its last input with time_s <= t (0 before its first input).
    Returns columns config, trial, time_s, edges, one row per grid point.
    """
    grid_frame = pl.DataFrame({"time_s": list(grid)}, schema={"time_s": pl.Float64})
    rows = []
    for (config, trial), frame in trials.group_by(["config", "trial"], maintain_order=True):
        steps = frame.select("time_s", "edges").sort("time_s")
        sampled = grid_frame.join_asof(steps, on="time_s", strategy="backward").with_columns(
            pl.col("edges").fill_null(0),
            pl.lit(config).alias("config"),
            pl.lit(trial).alias("trial"),
        )
        rows.append(sampled.select("config", "trial", "time_s", "edges"))
    return pl.concat(rows)
