"""Warm-up, part 1 of your submission: properties of a topological sort.

TODO(students): implement is_valid_toposort().

Grading calls it directly, with outputs of incorrect implementations and of
several correct ones. It must return True for every correct order and False
for every incorrect one, for any DAG as described in pbt/toposort.py.
Do not change the imports.
"""

from __future__ import annotations

from collections.abc import Sequence


def is_valid_toposort(
    nodes: Sequence[int], edges: Sequence[tuple[int, int]], order: Sequence[int]
) -> bool:
    """True if `order` is a topological order of the DAG (nodes, edges)."""
    raise NotImplementedError("warm-up: write the properties of a topological order")
