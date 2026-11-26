"""Main task: package dependency resolution. Do not modify; grading replaces
resolve() with other implementations, correct and incorrect.

A package index holds releases. Each release is one version of a package and
may require other packages, each within an inclusive range of versions. A
problem is an index plus the root requirements: what the user asked to
install. This is a simplified version of what pip, npm or cargo do.

Specification
-------------

A solution maps package names to versions. It is valid for a problem when:

  1. every chosen (package, version) is a release in the index;
  2. every root requirement is satisfied: its package is chosen, at a
     version inside the requirement's range;
  3. every requirement of every chosen release is satisfied in the same way;
  4. every chosen package is needed: it is reachable from the root
     requirements by following the requirements of chosen releases.

resolve(problem) returns a valid solution if one exists and None otherwise.
When several valid solutions exist, any of them is correct.

Requirements may name packages that have no releases at all (they cannot be
satisfied), the same package may be required several times with different
ranges, and requirements may form cycles (a release may even require its own
package).
"""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Mapping
from dataclasses import dataclass, field

type Solution = Mapping[str, int]

MAX_VERSION = 99


@dataclass(frozen=True)
class Requirement:
    """Needs `package` at a version v with low <= v <= high."""

    package: str
    low: int
    high: int

    def __post_init__(self) -> None:
        if not 1 <= self.low <= self.high <= MAX_VERSION:
            raise ValueError(f"invalid range [{self.low}, {self.high}] for {self.package}")

    def allows(self, version: int) -> bool:
        return self.low <= version <= self.high


@dataclass(frozen=True)
class Release:
    package: str
    version: int
    requires: tuple[Requirement, ...] = ()

    def __post_init__(self) -> None:
        if not 1 <= self.version <= MAX_VERSION:
            raise ValueError(f"invalid version {self.version} of {self.package}")


@dataclass(frozen=True)
class Problem:
    releases: tuple[Release, ...]
    root: tuple[Requirement, ...]
    _index: dict[tuple[str, int], Release] = field(init=False, repr=False, compare=False)

    def __post_init__(self) -> None:
        index: dict[tuple[str, int], Release] = {}
        for release in self.releases:
            key = (release.package, release.version)
            if key in index:
                raise ValueError(f"duplicate release {release.package} {release.version}")
            index[key] = release
        object.__setattr__(self, "_index", index)

    def release(self, package: str, version: int) -> Release | None:
        """The release of `package` at `version`, or None if there is none."""
        return self._index.get((package, version))

    def versions(self, package: str) -> list[int]:
        """The released versions of `package`, ascending (empty if none)."""
        return sorted(v for (p, v) in self._index if p == package)


def resolve(problem: Problem) -> dict[str, int] | None:
    """Returns a valid solution for `problem`, or None if none exists.

    Backtracking search: requirements are taken in order; a package's
    version is chosen (newest first) when it is first required, and every
    later requirement on it must accept that choice.
    """
    candidates: dict[str, list[Release]] = defaultdict(list)
    for release in problem.releases:
        candidates[release.package].append(release)
    for releases in candidates.values():
        releases.sort(key=lambda release: release.version, reverse=True)

    def search(chosen: dict[str, int], pending: list[Requirement]) -> dict[str, int] | None:
        if not pending:
            return chosen
        requirement, rest = pending[0], pending[1:]
        if requirement.package in chosen:
            if requirement.allows(chosen[requirement.package]):
                return search(chosen, rest)
            return None
        for release in candidates.get(requirement.package, []):
            if requirement.allows(release.version):
                found = search(
                    {**chosen, release.package: release.version}, rest + list(release.requires)
                )
                if found is not None:
                    return found
        return None

    return search({}, list(problem.root))
