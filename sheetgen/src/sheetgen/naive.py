"""Phase 2: a naive random-derivation generator.

TODO(students): implement generate(), following this rule exactly:

  * Start from START and repeatedly expand the leftmost remaining
    nonterminal until only text remains; join the text in order.
  * Depth: START has depth 0, and each symbol in an expansion has depth one
    more than the nonterminal it replaced.
  * A nonterminal shallower than max_depth: choose uniformly at random among
    all of its expansions in GRAMMAR.
  * A nonterminal at depth max_depth or deeper: choose uniformly at random
    among cheapest_expansions(symbol).

This always finishes. Grading checks that the sheets your generator writes
are distributed like those of this rule, so do not add weights, filters or
other knowledge of the language: improvements belong in adapted.py.

Hint: a stack of (symbol, depth) pairs, with each expansion pushed in
reverse order, expands the leftmost symbol first.
"""

from __future__ import annotations

import random

from sheetgen.grammar import (  # noqa: F401 (for your implementation)
    GRAMMAR,
    START,
    cheapest_expansions,
    is_nonterminal,
)


def generate(rng: random.Random, *, max_depth: int = 20) -> str:
    """Returns one sheet derived at random from GRAMMAR.

    All randomness must come from `rng`, so that a seed reproduces a corpus.
    """
    #raise NotImplementedError("Phase 2: implement random derivation from GRAMMAR")
    
    stack: list[tuple[str, int]] = [(START, 0)]
    output: list[str] = []

    while stack:
        symbol, depth = stack.pop()
        if not is_nonterminal(symbol):
            output.append(symbol)
            continue

        choices = (
            GRAMMAR[symbol]
            if depth < max_depth
            else cheapest_expansions(symbol)
        )
        expansion = rng.choice(choices)
        for child in reversed(expansion):
            stack.append((child, depth + 1))

    return "".join(output)
