from hypothesis import given

from pbt.dags import dags
from pbt.invariants import is_valid_toposort
from pbt.toposort import toposortish


@given(dags())
def test_toposortish_produces_a_valid_order(graph: tuple[list[int], list[tuple[int, int]]]) -> None:
    nodes, edges = graph
    order = toposortish(nodes, edges)
    assert is_valid_toposort(nodes, edges, order)
