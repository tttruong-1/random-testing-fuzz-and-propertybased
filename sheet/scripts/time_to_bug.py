#!/usr/bin/env python3
"""Sort saved failing inputs by symptom, check fixes, and tabulate time to bug.

    scripts/time_to_bug.py symptoms [--trials trials] [--repro PATH] [--out symptoms.tsv]
    scripts/time_to_bug.py minimize INPUT [--out min.sheet] [--repro PATH]
    scripts/time_to_bug.py compare symptoms.tsv symptoms-fixed.tsv
    scripts/time_to_bug.py table bugs.toml [--symptoms symptoms.tsv]

symptoms  replays every input in TRIALS/CONFIG/trial-K/artifacts/ with
          sheet-repro and writes one row per input: configuration, trial,
          time saved (from artifacts.csv), file, exit code and symptom. The
          symptom is the detector and where it fired: the failed
          standard-library hardening check or the sanitizer's error kind,
          with the first stack frame in src/; the UBSan message; the
          violated invariant; or "hang" if the input still runs after the
          timeout. It then prints each symptom with a count and an example.

minimize  shrinks INPUT while keeping its symptom, by delta debugging: it
          tries removing chunks of lines, then of characters, and keeps a
          removal only if sheet-repro still reports exactly the same
          symptom. libFuzzer's -minimize_crash keeps *any* crash, so it often
          slides from the bug you are studying to an easier one.

compare   for fix-and-replay. Replay the same inputs with a sheet-repro built
          from a patched copy of the source (symptoms --repro ... --out
          symptoms-fixed.tsv), then compare the two files. It lists, for
          each original symptom, how many inputs the patch stopped from
          failing, still fail the same way, or now fail differently.

table     labels each input with a bug from bugs.toml and prints the first
          time each bug was found in each trial, and per configuration the
          number of trials that found it and the median time to find it.

bugs.toml names your bugs and the symptoms that belong to each one. A symptom
pattern is a regular expression searched for in the symptom text:

    [[bug]]
    name = "short-name"
    symptoms = ["heap-use-after-free in Widget::grow", "invariant violated at src/x\\.cpp:12"]

Every input must match exactly one bug. Inputs that match none, or more than
one, are listed so you can fix the patterns. The median treats "not found" as
later than any time, so it is "not found" unless more than half of the trials
found the bug.
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import statistics
import subprocess
import sys
import tempfile
import tomllib
from collections import Counter, defaultdict
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

# Seconds a replay may run before the input counts as a hang. Longer than the
# fuzzers' 2-second limit, so slow but finite inputs are not mistaken for hangs.
REPLAY_TIMEOUT_S = 5
# While minimizing anything but a hang, a candidate that runs this long has
# turned into a different failure (often a hang), so it is rejected early.
CANDIDATE_TIMEOUT_S = 1

FIELDS = ("config", "trial", "time_s", "file", "exit", "symptom")
INVARIANT = re.compile(r"invariant violated at \S*?(src/\S+?): (.*)")
SANITIZER = re.compile(r"ERROR: (\w+Sanitizer): ([\w-]+)")
# libstdc++ hardening (_GLIBCXX_ASSERTIONS) prints this, then aborts.
HARDENING = re.compile(r"Assertion '(.+?)' failed")
UBSAN = re.compile(r"\S*?(src/[^:\s]+:\d+):\d+: runtime error: (.*)")
# A stack frame inside the program: "#3 0x... in FUNCTION /path/src/file.cpp:290:14"
PROGRAM_FRAME = re.compile(r"#\d+ 0x[0-9a-f]+ in (.+?) \S*?(src/[^:\s]+(?::\d+)?)", re.MULTILINE)
NOT_FOUND = "not found"


@dataclass(frozen=True)
class Failure:
    config: str
    trial: int
    time_s: float
    file: str  # path from the current directory
    exit: int
    symptom: str


# --- symptoms ------------------------------------------------------------------


def saved_times(trial_dir: Path) -> dict[str, float]:
    """When each artifact was saved, from artifacts.csv (seconds into the trial)."""
    index = trial_dir / "artifacts.csv"
    if not index.exists():
        return {}
    with index.open(newline="") as handle:
        return {row["file"]: float(row["time_s"]) for row in csv.DictReader(handle)}


def symptom_of(repro: Path, path: Path, timeout: float = REPLAY_TIMEOUT_S) -> tuple[int, str]:
    try:
        result = subprocess.run(
            [str(repro), str(path)],
            capture_output=True,
            text=True,
            errors="replace",
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return 124, f"hang (still running after {REPLAY_TIMEOUT_S} s)"
    symptom = describe(result.stderr + result.stdout)
    if symptom:
        return result.returncode, symptom
    if result.returncode in (0, 1):  # accepted, or rejected with a diagnostic
        return result.returncode, "no failure"
    return result.returncode, f"exit {result.returncode}, no report"


def short_function(name: str) -> str:
    """sheet::detail::(anonymous namespace)::Resolver::run() -> Resolver::run"""
    name = name.replace("(anonymous namespace)::", "").split("(", 1)[0]
    return name.removeprefix("sheet::").removeprefix("detail::")


def describe(report: str) -> str | None:
    """The detector and where it fired, stable across rebuilds."""
    if found := INVARIANT.search(report):
        return f"invariant violated at {found.group(1)}: {found.group(2).strip()}"
    if found := HARDENING.search(report):
        frame = PROGRAM_FRAME.search(report, found.end())
        where = f" in {short_function(frame.group(1))} ({frame.group(2)})" if frame else ""
        return f"hardening: '{found.group(1)}' failed{where}"
    if found := SANITIZER.search(report):
        detector, kind = found.groups()
        frame = PROGRAM_FRAME.search(report, found.end())
        where = f" in {short_function(frame.group(1))} ({frame.group(2)})" if frame else ""
        return f"{detector} {kind}{where}"
    if found := UBSAN.search(report):
        return f"UBSan: {found.group(2).strip()} ({found.group(1)})"
    return None


def replay(trials: Path, repro: Path) -> list[Failure]:
    jobs: list[tuple[str, int, float, Path]] = []
    for trial_dir in sorted(trials.glob("*/trial-*")):
        times = saved_times(trial_dir)
        trial = int(trial_dir.name.removeprefix("trial-"))
        for path in sorted((trial_dir / "artifacts").glob("*")):
            if path.is_file():
                jobs.append((trial_dir.parent.name, trial, times.get(path.name, -1.0), path))
    workers = max(1, (os.cpu_count() or 2) // 2)
    with ThreadPoolExecutor(workers) as pool:
        results = list(pool.map(lambda job: symptom_of(repro, job[3]), jobs))
    return [
        Failure(config, trial, time_s, str(path), code, symptom)
        for (config, trial, time_s, path), (code, symptom) in zip(jobs, results, strict=True)
    ]


def write_tsv(failures: list[Failure], out: Path) -> None:
    with out.open("w", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(FIELDS)
        for f in failures:
            writer.writerow([f.config, f.trial, f.time_s, f.file, f.exit, f.symptom])


def read_tsv(path: Path) -> list[Failure]:
    with path.open(newline="") as handle:
        return [
            Failure(
                row["config"],
                int(row["trial"]),
                float(row["time_s"]),
                row["file"],
                int(row["exit"]),
                row["symptom"],
            )
            for row in csv.DictReader(handle, delimiter="\t")
        ]


def command_symptoms(args: argparse.Namespace) -> int:
    if not os.access(args.repro, os.X_OK):
        print(f"error: {args.repro} is not built (see the repro preset)", file=sys.stderr)
        return 1
    failures = replay(args.trials, args.repro)
    write_tsv(failures, args.out)
    counts = Counter(f.symptom for f in failures)
    example = {f.symptom: f.file for f in reversed(failures)}
    print(f"{len(failures)} inputs replayed; {len(counts)} symptoms; written to {args.out}\n")
    for symptom, count in counts.most_common():
        print(f"{count:5d}  {symptom}\n       e.g. {example[symptom]}")
    return 0


# --- minimize ------------------------------------------------------------------


def ddmin(items: list[str], keeps_symptom: Callable[[list[str]], bool]) -> list[str]:
    """Zeller's delta debugging: a 1-minimal sublist that keeps the symptom."""
    n = 2
    while len(items) >= 2:
        size = -(-len(items) // n)
        chunks = [items[i : i + size] for i in range(0, len(items), size)]
        for i in range(len(chunks)):
            if keeps_symptom(chunks[i]):
                items, n = chunks[i], 2
                break
            complement = [x for j, chunk in enumerate(chunks) if j != i for x in chunk]
            if len(chunks) > 2 and keeps_symptom(complement):
                items, n = complement, max(n - 1, 2)
                break
        else:
            if n >= len(items):
                break
            n = min(n * 2, len(items))
    return items


def command_minimize(args: argparse.Namespace) -> int:
    original = args.input.read_text(errors="surrogateescape")
    _, target = symptom_of(args.repro, args.input)
    if target == "no failure":
        print(f"error: {args.input} does not fail", file=sys.stderr)
        return 1
    print(f"keeping: {target}")
    timeout = REPLAY_TIMEOUT_S if target.startswith("hang") else CANDIDATE_TIMEOUT_S
    tests = 0
    with tempfile.TemporaryDirectory() as scratch:
        candidate = Path(scratch) / "candidate.sheet"
        cache: dict[str, bool] = {}

        def keeps(text: str) -> bool:
            nonlocal tests
            if text not in cache:
                if tests >= args.max_runs:
                    return False  # out of budget: keep what we have
                tests += 1
                candidate.write_text(text, errors="surrogateescape")
                cache[text] = symptom_of(args.repro, candidate, timeout)[1] == target
            return cache[text]

        lines = ddmin(original.splitlines(keepends=True), lambda part: keeps("".join(part)))
        chars = ddmin(list("".join(lines)), lambda part: keeps("".join(part)))
    result = "".join(chars)
    args.out.write_text(result, errors="surrogateescape")
    print(f"{len(original)} -> {len(result)} bytes after {tests} runs; written to {args.out}")
    if tests >= args.max_runs:
        print(f"stopped at --max-runs {args.max_runs}; run minimize again on {args.out} to go on")
    return 0


# --- compare -------------------------------------------------------------------


def command_compare(args: argparse.Namespace) -> int:
    before = {f.file: f for f in read_tsv(args.original)}
    after = {f.file: f for f in read_tsv(args.fixed)}
    outcome: dict[str, Counter[str]] = defaultdict(Counter)
    changed: dict[str, Counter[str]] = defaultdict(Counter)
    for file, old in before.items():
        new = after.get(file)
        if new is None:
            outcome[old.symptom]["not replayed"] += 1
        elif new.symptom == "no failure":
            outcome[old.symptom]["fixed"] += 1
        elif new.symptom == old.symptom:
            outcome[old.symptom]["unchanged"] += 1
        else:
            outcome[old.symptom]["different"] += 1
            changed[old.symptom][new.symptom] += 1
    print("fixed  same  different  symptom before the patch")
    for symptom, counts in sorted(outcome.items(), key=lambda kv: -kv[1]["fixed"]):
        print(f"{counts['fixed']:5d} {counts['unchanged']:5d} {counts['different']:10d}  {symptom}")
        for new_symptom, n in changed[symptom].most_common():
            print(f"{'':23}-> {n} now: {new_symptom}")
        if counts["not replayed"]:
            print(f"{'':23}({counts['not replayed']} missing from the second file)")
    return 0


# --- table ---------------------------------------------------------------------


def load_bugs(path: Path) -> list[tuple[str, list[re.Pattern[str]]]]:
    with path.open("rb") as handle:
        raw = tomllib.load(handle)
    bugs = []
    for entry in raw.get("bug", []):
        patterns = [re.compile(pattern) for pattern in entry.get("symptoms", [])]
        bugs.append((str(entry["name"]), patterns))
    if not bugs:
        raise ValueError(f"{path}: no [[bug]] entries")
    return bugs


def median_time(times: list[float | None]) -> str:
    """Median with "not found" ranked after every time."""
    ranked = sorted(times, key=lambda t: (t is None, t or 0.0))
    if not ranked:
        return NOT_FOUND
    n = len(ranked)
    middle = ranked[(n - 1) // 2 : n // 2 + 1]
    if any(t is None for t in middle):
        return NOT_FOUND
    return f"{statistics.mean(t for t in middle if t is not None):.0f}"


def command_table(args: argparse.Namespace) -> int:
    bugs = load_bugs(args.bugs)
    failures = read_tsv(args.symptoms)
    first: dict[tuple[str, str, int], float] = {}
    trials: dict[str, set[int]] = defaultdict(set)
    unmatched: Counter[str] = Counter()
    ambiguous: Counter[str] = Counter()
    for trial_dir in sorted(Path(args.trials).glob("*/trial-*")):
        trials[trial_dir.parent.name].add(int(trial_dir.name.removeprefix("trial-")))
    for f in failures:
        trials[f.config].add(f.trial)
        if f.symptom == "no failure":
            continue
        names = [name for name, patterns in bugs if any(p.search(f.symptom) for p in patterns)]
        if len(names) != 1:
            (ambiguous if names else unmatched)[f.symptom] += 1
            continue
        key = (names[0], f.config, f.trial)
        if f.time_s >= 0 and (key not in first or f.time_s < first[key]):
            first[key] = f.time_s

    configs = sorted(trials)
    width = max((max(t) for t in trials.values() if t), default=0)
    print("## First found (seconds into the trial), per trial\n")
    print("| Bug | Config | " + " | ".join(f"Trial {k}" for k in range(1, width + 1)) + " |")
    print("| --- | --- |" + " --- |" * width)
    for name, _ in bugs:
        for config in configs:
            cells = []
            for k in range(1, width + 1):
                if k not in trials[config]:
                    cells.append("")
                elif (name, config, k) in first:
                    cells.append(f"{first[(name, config, k)]:.0f}")
                else:
                    cells.append(NOT_FOUND)
            print(f"| {name} | {config} | " + " | ".join(cells) + " |")

    print("\n## Summary: trials that found the bug, and median time to find it (s)\n")
    print("| Bug | " + " | ".join(configs) + " |")
    print("| --- |" + " --- |" * len(configs))
    for name, _ in bugs:
        cells = []
        for config in configs:
            times = [first.get((name, config, k)) for k in sorted(trials[config])]
            found = sum(t is not None for t in times)
            cells.append(f"{found}/{len(times)}, {median_time(times)}")
        print(f"| {name} | " + " | ".join(cells) + " |")

    for title, counter in (
        ("match no bug in bugs.toml", unmatched),
        ("match more than one bug", ambiguous),
    ):
        if counter:
            print(f"\nInputs whose symptoms {title} (not counted above):")
            for symptom, n in counter.most_common():
                print(f"  {n:5d}  {symptom}")
    return 1 if unmatched or ambiguous else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    symptoms = sub.add_parser("symptoms", help="replay saved inputs and sort them by symptom")
    symptoms.add_argument("--trials", type=Path, default=Path("trials"))
    symptoms.add_argument("--repro", type=Path, default=Path("build/repro/sheet-repro"))
    symptoms.add_argument("--out", type=Path, default=Path("symptoms.tsv"))

    minimize = sub.add_parser("minimize", help="shrink an input, keeping its symptom")
    minimize.add_argument("input", type=Path)
    minimize.add_argument("--out", type=Path, default=Path("min.sheet"))
    minimize.add_argument("--repro", type=Path, default=Path("build/repro/sheet-repro"))
    minimize.add_argument("--max-runs", type=int, default=10000)

    compare = sub.add_parser("compare", help="compare replays before and after a patch")
    compare.add_argument("original", type=Path)
    compare.add_argument("fixed", type=Path)

    table = sub.add_parser("table", help="tabulate time to bug from a bugs.toml")
    table.add_argument("bugs", type=Path)
    table.add_argument("--symptoms", type=Path, default=Path("symptoms.tsv"))
    table.add_argument("--trials", type=Path, default=Path("trials"))

    args = parser.parse_args()
    handlers = {
        "symptoms": command_symptoms,
        "minimize": command_minimize,
        "compare": command_compare,
        "table": command_table,
    }
    return handlers[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
