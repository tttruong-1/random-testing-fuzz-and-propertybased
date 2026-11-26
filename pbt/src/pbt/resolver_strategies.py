"""Main task, part 3 of your submission: generators of resolution problems.

TODO(students): implement problems() and planted_problems().

Grading runs your generators against incorrect resolvers. Some are wrong on
almost any input; others are wrong only on inputs a naive generator rarely
produces. Your generators are measured by how many of those they expose,
with a fixed number of examples per test.

  * problems(): any well-formed problems, satisfiable or not. Every
    Problem you build is checked when it is constructed (see the dataclasses
    in pbt/resolver.py).

  * planted_problems(): pairs (problem, witness) where `witness` is a valid
    solution for `problem` by construction. A resolver that returns None for
    such a problem is wrong. Every witness you produce is checked against a
    reference implementation of the properties; a single invalid witness
    makes this generator score zero.

Think about what makes resolution hard, and make sure your generators
produce it often. Filtering with assume() or .filter() to reach the
interesting cases tends to fail Hypothesis's health checks; construct them
instead. Do not change the imports, except to add more from hypothesis.
"""

from __future__ import annotations

from hypothesis import strategies as st

from pbt.resolver import Problem


def problems() -> st.SearchStrategy[Problem]:
    """Well-formed problems, satisfiable or not."""
    raise NotImplementedError("write a generator of problems")


def planted_problems() -> st.SearchStrategy[tuple[Problem, dict[str, int]]]:
    """Problems paired with a valid solution built in."""
    raise NotImplementedError("write a generator of problems with a planted solution")
