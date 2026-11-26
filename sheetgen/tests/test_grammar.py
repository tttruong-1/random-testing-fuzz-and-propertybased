from sheetgen.grammar import (
    GRAMMAR,
    START,
    cheapest_expansions,
    expansion_cost,
    is_nonterminal,
    referenced_nonterminals,
    symbol_cost,
)


def test_start_symbol_is_defined() -> None:
    assert START in GRAMMAR


def test_every_referenced_nonterminal_is_defined() -> None:
    undefined = {symbol for symbol in referenced_nonterminals(GRAMMAR) if symbol not in GRAMMAR}
    assert undefined == set()


def test_every_nonterminal_has_an_expansion() -> None:
    assert all(GRAMMAR[name] for name in GRAMMAR)


def test_nonterminal_detection() -> None:
    assert is_nonterminal("<expr>")
    assert not is_nonterminal("<")
    assert not is_nonterminal("<>")  # the not-equal operator is terminal text
    assert not is_nonterminal("SUM")


def test_costs_of_simple_symbols() -> None:
    assert symbol_cost("SUM") == 0
    assert symbol_cost("<digit>") == 1
    assert symbol_cost("<lines>") == 1  # the empty expansion
    assert cheapest_expansions("<lines>") == [()]


def test_cheapest_expansions_handle_left_recursion() -> None:
    # <additive> := <term> | <additive> op <term>: the recursive option is never cheapest.
    assert cheapest_expansions("<additive>") == [("<term>",)]


def test_cheapest_expansions_always_get_closer_to_finishing() -> None:
    for name in GRAMMAR:
        for expansion in cheapest_expansions(name):
            assert expansion_cost(expansion) == symbol_cost(name) - 1
            assert all(symbol_cost(s) < symbol_cost(name) for s in expansion)
