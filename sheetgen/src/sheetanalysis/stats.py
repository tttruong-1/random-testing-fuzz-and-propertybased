"""Statistics for comparing fuzzing configurations across trials."""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass

from scipy import stats


@dataclass(frozen=True)
class MannWhitneyResult:
    u: float
    p_value: float
    # Probability that a random trial of `a` beats a random trial of `b`
    # (ties count half); 0.5 means no difference.
    a_beats_b: float


def mann_whitney(a: Sequence[float], b: Sequence[float]) -> MannWhitneyResult:
    """Two-sided Mann-Whitney U test of samples `a` and `b`.

    Use it on one number per trial, such as final edge counts. With 5 trials
    per configuration, the smallest possible p-value is about 0.008.
    """
    if not a or not b:
        raise ValueError("both samples need at least one trial")
    result = stats.mannwhitneyu(list(a), list(b), alternative="two-sided")
    u = float(result.statistic)
    return MannWhitneyResult(u=u, p_value=float(result.pvalue), a_beats_b=u / (len(a) * len(b)))
