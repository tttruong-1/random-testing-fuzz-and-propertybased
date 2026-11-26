"""A few example-based checks of your properties, to get you started. Passing
them does not mean your properties are complete."""

from pbt.invariants import is_valid_toposort
from pbt.resolver import Problem, Release, Requirement
from pbt.resolver_properties import is_valid_solution


def test_toposort_examples() -> None:
    assert is_valid_toposort([1, 2, 3], [(1, 2), (2, 3)], [1, 2, 3])
    assert not is_valid_toposort([1, 2, 3], [(1, 2), (2, 3)], [3, 2, 1])
    assert is_valid_toposort([], [], [])


def test_resolution_examples() -> None:
    problem = Problem(
        releases=(
            Release("app", 1, (Requirement("lib", 2, 3),)),
            Release("lib", 1),
            Release("lib", 3),
        ),
        root=(Requirement("app", 1, 1),),
    )
    assert is_valid_solution(problem, {"app": 1, "lib": 3})
    assert not is_valid_solution(problem, {"app": 1, "lib": 1})  # lib 1 is out of range
    assert not is_valid_solution(problem, {"app": 1})  # lib is missing
    assert is_valid_solution(Problem(releases=(), root=()), {})
