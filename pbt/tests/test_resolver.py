"""The tests grading builds from your submission. Grading runs them with a
fixed number of examples, substituting other implementations of resolve()
and, for some parts, reference versions of your properties or generators.
"""

from hypothesis import given

from pbt.resolver import Problem, resolve
from pbt.resolver_properties import is_valid_solution
from pbt.resolver_strategies import planted_problems, problems


@given(problems())
def test_any_solution_returned_is_valid(problem: Problem) -> None:
    solution = resolve(problem)
    if solution is not None:
        assert is_valid_solution(problem, solution)


@given(planted_problems())
def test_problems_with_a_solution_are_solved(case: tuple[Problem, dict[str, int]]) -> None:
    problem, witness = case
    assert is_valid_solution(problem, witness), "the planted witness should be valid"
    solution = resolve(problem)
    assert solution is not None, "a solution exists, but resolve() found none"
    assert is_valid_solution(problem, solution)
