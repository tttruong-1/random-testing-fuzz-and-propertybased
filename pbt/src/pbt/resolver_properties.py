"""Main task, part 2 of your submission: properties of a resolution.

TODO(students): implement is_valid_solution().

It must return True exactly when `solution` is valid for `problem` according
to the specification in pbt/resolver.py. Grading calls it with outputs of
incorrect resolvers and of several correct ones, including correct resolvers
that choose different solutions than the provided one.

Hint: the specification has four conditions. A function that checks three
of them will accept some incorrect resolvers.
"""

from __future__ import annotations

from pbt.resolver import Problem, Solution


def is_valid_solution(problem: Problem, solution: Solution) -> bool:
    """True if `solution` is a valid solution for `problem`."""
    raise NotImplementedError("write the properties of a valid solution")
