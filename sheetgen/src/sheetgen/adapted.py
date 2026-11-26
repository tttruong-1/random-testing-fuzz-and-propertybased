"""Phase 2 adaptation: your improved generator.

TODO(students): implement generate().

Before changing anything, write down your hypothesis: which pipeline stage
or code your change should reach, and why the naive generator cannot. Then
make one targeted change and measure its effect with run-trials and covtrace.

You may reuse code from naive.py.
"""

from __future__ import annotations

import random

from sheetgen.grammar import (
    GRAMMAR,
    START,
    Expansion,
    cheapest_expansions,
    expansion_cost,
    is_nonterminal,
)

ADAPTED_GRAMMAR: dict[str, list[Expansion]] = {
    **GRAMMAR,
    "<col>": [(c,) for c in "ABCD"],
    "<row>": [(r,) for r in "1234"],
}


def _cheapest(symbol: str) -> list[Expansion]:
    if symbol in ("<col>", "<row>"):
        return ADAPTED_GRAMMAR[symbol]
    return cheapest_expansions(symbol)

def generate(rng: random.Random, *, max_depth: int = 20) -> str:
    """Returns one sheet. All randomness must come from `rng`."""
    #raise NotImplementedError("Phase 2: implement your adapted generator")

    stack: list[tuple[str, int]] = [(START, 0)]
    output: list[str] = []

    while stack:
        symbol, depth = stack.pop()
        if not is_nonterminal(symbol):
            output.append(symbol)
            continue

        choices = ADAPTED_GRAMMAR[symbol] if depth < max_depth else _cheapest(symbol)
        expansion = rng.choice(choices)
        for child in reversed(expansion):
            stack.append((child, depth + 1))

    return "".join(output)
