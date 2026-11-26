#!/usr/bin/env python3
"""Run repeated fuzzing trials of one configuration and trace their coverage.

    scripts/run_trials.py configs/B0.toml [--trials N] [--duration SECONDS] [--out DIR]

Each trial gets its own directory, OUT/NAME/trial-K, containing:

    corpus/        inputs kept because they added coverage
    artifacts/     crashing, timing-out and OOM inputs
    fuzz.log       the fuzzer's output
    trial.json     the configuration as run, the trial number, when it
                   started and how long fuzzing actually ran (written when
                   the trial ends; graders check it against your configs)
    covtrace.csv   coverage over time, produced by covtrace after the trial
    artifacts.csv  when each failing input was saved (time_s, file)

Configuration keys (TOML):

    name        configuration name, e.g. "B0"
    kind        "libfuzzer" or "generator"
    trials      number of trials (default 5)
    duration_s  seconds per trial (default 1800)
    fuzzer      path to the libFuzzer binary (default build/fuzz/fuzz_sheet)
    covtrace    path to covtrace (default build/covtrace/covtrace)
    seeds       seed corpus directory (default seeds); libfuzzer kind only
    extra_args  extra libFuzzer arguments, e.g. ["-dict=sheet.dict", "-max_len=4096"]
    generator   generator command, without --count/--seed/--out; generator kind only
    batch_size  inputs per generated batch (default 100); generator kind only

Trial K uses seed K, so a configuration's trials are reproducible.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time
import tomllib
from dataclasses import asdict, dataclass, field
from pathlib import Path

# Seconds an input may run before it counts as a hang. Normal inputs finish
# in milliseconds; every configuration uses the same limit, so a hang costs
# libFuzzer and generator trials the same time.
HANG_TIMEOUT_S = 2

# Inputs that may still be merged after a generator trial's deadline: the
# merge of the last batch is given this much grace before it is cut off.
MERGE_GRACE_S = 30

# libFuzzer flags shared by every trial. Fork mode keeps fuzzing after
# crashes, timeouts and OOMs; -reduce_inputs=0 keeps earlier corpus inputs so
# covtrace can reconstruct the coverage curve.
LIBFUZZER_FLAGS = [
    "-fork=1",
    "-ignore_crashes=1",
    "-ignore_timeouts=1",
    "-ignore_ooms=1",
    "-reduce_inputs=0",
    f"-timeout={HANG_TIMEOUT_S}",
]


@dataclass(frozen=True)
class Config:
    name: str
    kind: str
    trials: int = 5
    duration_s: int = 1800
    fuzzer: Path = Path("build/fuzz/fuzz_sheet")
    covtrace: Path = Path("build/covtrace/covtrace")
    seeds: Path = Path("seeds")
    extra_args: list[str] = field(default_factory=list)
    generator: list[str] = field(default_factory=list)
    batch_size: int = 100

    @staticmethod
    def load(path: Path) -> Config:
        with path.open("rb") as handle:
            raw = tomllib.load(handle)
        for key in ("fuzzer", "covtrace", "seeds"):
            if key in raw:
                raw[key] = Path(raw[key])
        config = Config(**raw)
        if config.kind not in ("libfuzzer", "generator"):
            raise ValueError(f"{path}: kind must be 'libfuzzer' or 'generator'")
        if config.kind == "generator" and not config.generator:
            raise ValueError(f"{path}: generator configurations need a 'generator' command")
        return config


def run_libfuzzer_trial(config: Config, trial: int, directory: Path) -> None:
    corpus = directory / "corpus"
    artifacts = directory / "artifacts"
    corpus.mkdir()
    artifacts.mkdir()
    for seed in sorted(config.seeds.iterdir()):
        if seed.is_file():
            shutil.copy(seed, corpus / seed.name)

    command = [
        str(config.fuzzer),
        str(corpus),
        *LIBFUZZER_FLAGS,
        f"-max_total_time={config.duration_s}",
        f"-seed={trial}",
        f"-artifact_prefix={artifacts}/",
        *config.extra_args,
    ]
    with (directory / "fuzz.log").open("w") as log:
        print("$", " ".join(command), file=log, flush=True)
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)


# Signs in a merge log that some input in the batch crashed or hung.
MERGE_FAILURE = re.compile(r"ERROR: AddressSanitizer|ERROR: libFuzzer|deadly signal|"
                           r"runtime error|invariant violated|ALARM: working on the last Unit")


def run_generator_trial(config: Config, trial: int, directory: Path) -> None:
    """Generates batches and runs them in-process through the fuzzer binary.

    Each batch is merged into the corpus with libFuzzer's -merge=1, which runs
    every input, survives crashes, and keeps only inputs that add coverage, so
    generator corpora are comparable to libFuzzer corpora. When a merge shows
    a crash or hang, the batch is replayed through covtrace (one process per
    input) to find the failing inputs, which are saved to artifacts/.
    """
    corpus = directory / "corpus"
    artifacts = directory / "artifacts"
    corpus.mkdir()
    artifacts.mkdir()
    (directory / "merge-scratch").mkdir()
    deadline = time.monotonic() + config.duration_s
    batch = 0
    with (directory / "fuzz.log").open("w") as log:
        while time.monotonic() < deadline:
            batch_dir = directory / "batch"
            shutil.rmtree(batch_dir, ignore_errors=True)
            generate = [
                *config.generator,
                "--count", str(config.batch_size),
                "--seed", str(trial * 1_000_000 + batch),
                "--out", str(batch_dir),
            ]
            print("$", " ".join(generate), file=log, flush=True)
            subprocess.run(generate, stdout=log, stderr=subprocess.STDOUT, check=True)
            # Merge writes its own crash files; keep them out of the working
            # directory (save_failures records the failing inputs instead).
            merge = [
                str(config.fuzzer), "-merge=1", f"-timeout={HANG_TIMEOUT_S}",
                f"-artifact_prefix={directory / 'merge-scratch'}/", str(corpus), str(batch_dir),
            ]
            # Small batches keep corpus timestamps fine-grained; the cut-off
            # keeps a slow last batch from running far past the deadline.
            remaining = max(deadline - time.monotonic(), 0.0)
            try:
                merged = subprocess.run(merge, capture_output=True, text=True, check=False,
                                        timeout=remaining + MERGE_GRACE_S)
            except subprocess.TimeoutExpired:
                print("# last batch cut off at the deadline", file=log, flush=True)
                break
            log.write(merged.stdout + merged.stderr)
            if MERGE_FAILURE.search(merged.stdout + merged.stderr):
                save_failures(config, batch, batch_dir, artifacts)
            batch += 1
        shutil.rmtree(directory / "batch", ignore_errors=True)
        shutil.rmtree(directory / "merge-scratch", ignore_errors=True)


# A generator that keeps producing the same crash would otherwise flood
# artifacts/. The first few inputs per batch for each distinct failure status
# (exit code, signal or timeout) are enough for time-to-bug tables, without
# letting a common crash hide a rarer one.
MAX_SAVED_PER_STATUS = 5


def save_failures(config: Config, batch: int, batch_dir: Path, artifacts: Path) -> None:
    """Copies inputs in `batch_dir` that crash or hang into `artifacts`, up to
    MAX_SAVED_PER_STATUS for each failure status."""
    replay = subprocess.run(
        [str(config.covtrace), "--timeout", str(HANG_TIMEOUT_S), str(batch_dir)],
        capture_output=True, text=True, check=False,
    )
    saved: dict[str, int] = {}
    for row in csv.DictReader(io.StringIO(replay.stdout)):
        status = row["status"]
        if status != "ok" and saved.get(status, 0) < MAX_SAVED_PER_STATUS:
            saved[status] = saved.get(status, 0) + 1
            kind = "timeout" if row["status"] == "timeout" else "crash"
            shutil.copy(batch_dir / row["file"], artifacts / f"{kind}-b{batch}-{row['file']}")


def index_artifacts(directory: Path, start: int) -> None:
    """Writes artifacts.csv: when each failing input was saved, for time-to-bug tables."""
    rows = sorted(
        (path.stat().st_mtime - start, path.name)
        for path in (directory / "artifacts").iterdir()
        if path.is_file()
    )
    with (directory / "artifacts.csv").open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(["time_s", "file"])
        writer.writerows((f"{t:.3f}", name) for t, name in rows)


def trace(config: Config, directory: Path, start: int) -> None:
    command = [str(config.covtrace), "--start", str(start), str(directory / "corpus")]
    with (directory / "covtrace.csv").open("w") as output:
        subprocess.run(command, stdout=output, stderr=sys.stderr, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("config", type=Path)
    parser.add_argument("--trials", type=int, help="override the configuration's trial count")
    parser.add_argument("--duration", type=int, help="override seconds per trial")
    parser.add_argument("--out", type=Path, default=Path("trials"))
    args = parser.parse_args()

    config = Config.load(args.config)
    trials = args.trials or config.trials
    if args.duration:
        config = Config(**{**config.__dict__, "duration_s": args.duration})

    for tool in (config.fuzzer, config.covtrace):
        if not os.access(tool, os.X_OK):
            print(f"error: {tool} is not built (see the fuzz and covtrace presets)", file=sys.stderr)
            return 1

    for trial in range(1, trials + 1):
        directory = args.out / config.name / f"trial-{trial}"
        if directory.exists():
            print(f"error: {directory} exists; remove it or choose another --out", file=sys.stderr)
            return 1
        directory.mkdir(parents=True)
        start = int(time.time())
        began = time.monotonic()
        print(f"[{config.name}] trial {trial}/{trials} ({config.duration_s}s)", flush=True)
        if config.kind == "libfuzzer":
            run_libfuzzer_trial(config, trial, directory)
        else:
            run_generator_trial(config, trial, directory)
        fuzzing_s = round(time.monotonic() - began, 1)
        trace(config, directory, start)
        index_artifacts(directory, start)
        record_trial(config, trial, directory, start, fuzzing_s, args.config)
    return 0


def record_trial(
    config: Config, trial: int, directory: Path, start: int, fuzzing_s: float, source: Path
) -> None:
    """Writes trial.json: what ran and for how long, for graders to check."""
    settings = {key: str(value) if isinstance(value, Path) else value
                for key, value in asdict(config).items()}
    record = {
        "config_file": str(source),
        "settings": settings,
        "trial": trial,
        "started_at": start,
        "fuzzing_s": fuzzing_s,
    }
    (directory / "trial.json").write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    sys.exit(main())
