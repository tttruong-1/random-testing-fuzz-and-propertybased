"""The sheet language as a context-free grammar, provided as data.

A grammar maps each nonterminal (a name in angle brackets, such as "<expr>")
to its expansions. An expansion is a tuple of symbols; a symbol is either a
nonterminal or terminal text that is copied to the output as-is. An empty
tuple expands to nothing.

This grammar describes syntax only. Some sheets it derives are still invalid:
for example, it can reference a cell that is never defined. That is
deliberate, and part of what Phase 2 asks you to investigate.

For depth control, cheapest_expansions(symbol) gives the expansions of a
nonterminal that turn into text in the fewest steps (see the bottom of this
file).
"""

from __future__ import annotations

import string
from collections.abc import Iterator, Mapping, Sequence
from functools import cache
from typing import Final

type Symbol = str
type Expansion = tuple[Symbol, ...]
type Grammar = Mapping[str, Sequence[Expansion]]

START: Final = "<sheet>"

FUNCTIONS: Final = ("SUM", "MIN", "MAX", "AVERAGE", "COUNT", "IF", "CONCAT", "LEN", "ROUND")


def _choices(*terminals: str) -> list[Expansion]:
    return [(terminal,) for terminal in terminals]


GRAMMAR: Final[Grammar] = {
    "<sheet>": [("#sheet v1\n", "<lines>")],
    "<lines>": [(), ("<line>", "<lines>")],
    "<line>": [
        ("<cell>", " = ", "<expr>", "\n"),
        ("# a comment\n",),
        ("\n",),
    ],
    "<expr>": [("<additive>",), ("<additive>", " ", "<cmp_op>", " ", "<additive>")],
    "<cmp_op>": _choices("==", "<>", "<", ">", "<=", ">="),
    "<additive>": [("<term>",), ("<additive>", " ", "<add_op>", " ", "<term>")],
    "<add_op>": _choices("+", "-", "&"),
    "<term>": [("<unary>",), ("<term>", " ", "<mul_op>", " ", "<unary>")],
    "<mul_op>": _choices("*", "/"),
    "<unary>": [("-", "<unary>"), ("<primary>",)],
    "<primary>": [
        ("<number>",),
        ("<string>",),
        ("TRUE",),
        ("FALSE",),
        ("<cell>",),
        ("<call>",),
        ("(", "<expr>", ")"),
    ],
    "<call>": [("<fname>", "()"), ("<fname>", "(", "<args>", ")")],
    "<fname>": _choices(*FUNCTIONS),
    "<args>": [("<arg>",), ("<arg>", ", ", "<args>")],
    "<arg>": [("<expr>",), ("<range>",)],
    "<range>": [("<cell>", ":", "<cell>")],
    "<cell>": [("<col>", "<row>")],
    "<col>": [("<letter>",), ("<letter>", "<col>")],
    "<letter>": _choices(*string.ascii_uppercase),
    "<row>": [("<nonzero>",), ("<nonzero>", "<digits>")],
    "<number>": [("<digits>",), ("<digits>", ".", "<digits>")],
    "<digits>": [("<digit>",), ("<digit>", "<digits>")],
    "<digit>": _choices(*string.digits),
    "<nonzero>": _choices(*"123456789"),
    "<string>": [('"', "<chars>", '"')],
    "<chars>": [(), ("<char>", "<chars>")],
    "<char>": [*_choices(*string.ascii_letters, *string.digits, " ", ":", "!"), ("\\", "<escape>")],
    "<escape>": _choices('"', "\\", "n", "t"),
}


def is_nonterminal(symbol: Symbol) -> bool:
    """Returns True for names in angle brackets, such as "<expr>"."""
    return len(symbol) > 2 and symbol.startswith("<") and symbol.endswith(">")


def referenced_nonterminals(grammar: Grammar) -> Iterator[str]:
    """Yields every nonterminal used in some expansion, with repeats."""
    for expansions in grammar.values():
        for expansion in expansions:
            yield from (symbol for symbol in expansion if is_nonterminal(symbol))


# --- Depth control --------------------------------------------------------------
#
# The cost of a symbol is the fewest expansion steps needed to turn it into
# text: 0 for terminal text, and for a nonterminal 1 plus the cost of its
# cheapest expansion. Choosing only cheapest expansions always finishes,
# because every symbol in a cheapest expansion costs strictly less than the
# symbol it replaces.


@cache
def _costs() -> dict[str, int]:
    # Fixed-point iteration: start every nonterminal at infinity and lower the
    # costs until nothing changes. Unlike a recursive definition, this cannot
    # loop forever on left-recursive rules such as <additive>.
    infinity = float("inf")
    costs: dict[str, float] = dict.fromkeys(GRAMMAR, infinity)

    def cost_of(symbol: Symbol) -> float:
        return costs[symbol] if is_nonterminal(symbol) else 0

    changed = True
    while changed:
        changed = False
        for name, expansions in GRAMMAR.items():
            best = min(1 + sum(cost_of(s) for s in expansion) for expansion in expansions)
            if best < costs[name]:
                costs[name] = best
                changed = True
    unfinishable = [name for name, cost in costs.items() if cost == infinity]
    if unfinishable:
        raise ValueError(f"these nonterminals can never finish: {unfinishable}")
    return {name: int(cost) for name, cost in costs.items()}


def symbol_cost(symbol: Symbol) -> int:
    """The fewest expansion steps that turn `symbol` into text (0 for text)."""
    return _costs()[symbol] if is_nonterminal(symbol) else 0


def expansion_cost(expansion: Expansion) -> int:
    """The fewest expansion steps that turn every symbol of `expansion` into text."""
    return sum(symbol_cost(symbol) for symbol in expansion)


def cheapest_expansions(symbol: str) -> list[Expansion]:
    """The expansions of the nonterminal `symbol` that finish in the fewest steps."""
    expansions = GRAMMAR[symbol]
    least = min(expansion_cost(expansion) for expansion in expansions)
    return [expansion for expansion in expansions if expansion_cost(expansion) == least]
