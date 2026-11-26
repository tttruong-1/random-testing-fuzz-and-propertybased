"""Warm-up: topological sorting. Do not modify; grading may replace it.

toposortish(nodes, edges) takes the nodes of a directed acyclic graph (DAG),
labelled by distinct integers, and its directed edges as (source, target)
pairs between those nodes. It returns a new list containing every node
exactly once, in an order where each node comes before all of its
successors. A DAG usually has many such orders and any of them is correct.
"""

from __future__ import annotations

from collections.abc import Sequence
from graphlib import TopologicalSorter


def toposortish(nodes: Sequence[int], edges: Sequence[tuple[int, int]]) -> list[int]:
    sorter: TopologicalSorter[int] = TopologicalSorter({node: set() for node in nodes})
    for source, target in edges:
        sorter.add(target, source)
    return list(sorter.static_order())
