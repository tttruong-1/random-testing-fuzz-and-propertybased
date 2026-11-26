"""A provided generator of directed acyclic graphs for the warm-up.

Nodes get distinct integer labels in a random order; an edge may only go
from an earlier node to a later one in a hidden ordering, which guarantees
there are no cycles. Duplicate edges are allowed.
"""

from __future__ import annotations

from hypothesis import strategies as st


@st.composite
def dags(draw: st.DrawFn) -> tuple[list[int], list[tuple[int, int]]]:
    nodes = draw(st.lists(st.integers(-50, 50), unique=True, max_size=12))
    hidden_order = draw(st.permutations(nodes))
    edges: list[tuple[int, int]] = []
    for i, source in enumerate(hidden_order):
        for target in hidden_order[i + 1 :]:
            if draw(st.integers(0, 3)) == 0:
                edges.append((source, target))
    if len(edges) > 0 and draw(st.booleans()):
        edges.append(draw(st.sampled_from(edges)))
    return nodes, draw(st.permutations(edges))
