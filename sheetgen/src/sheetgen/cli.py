"""Command-line interface for the generators. Do not change its interface:
grading regenerates your corpora with it.

    uv run sheetgen {naive,adapted} --count N --seed S --out DIR [--max-depth D]
"""

from __future__ import annotations

import argparse
import random
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Final, Protocol

from sheetgen import adapted, naive, x


class Generator(Protocol):
    def __call__(self, rng: random.Random, *, max_depth: int = ...) -> str: ...


GENERATORS: Final[Mapping[str, Generator]] = {
    "naive": naive.generate,
    "adapted": adapted.generate,
    "x": x.generate,
}


def write_corpus(generator: Generator, count: int, seed: int, out: Path, max_depth: int) -> None:
    """Writes `count` inputs to `out`, named 00000000.sheet, 00000001.sheet, ..."""
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(seed)
    for index in range(count):
        text = generator(rng, max_depth=max_depth)
        (out / f"{index:08d}.sheet").write_text(text, encoding="utf-8")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="sheetgen", description="Generate sheet inputs.")
    parser.add_argument("generator", choices=sorted(GENERATORS))
    parser.add_argument("--count", type=int, required=True, help="number of inputs")
    parser.add_argument("--seed", type=int, required=True, help="random seed")
    parser.add_argument("--out", type=Path, required=True, help="output directory")
    parser.add_argument("--max-depth", type=int, default=20, help="derivation depth hint")
    args = parser.parse_args(argv)

    if args.count < 0:
        parser.error("--count must not be negative")
    write_corpus(GENERATORS[args.generator], args.count, args.seed, args.out, args.max_depth)
    return 0


if __name__ == "__main__":
    sys.exit(main())
