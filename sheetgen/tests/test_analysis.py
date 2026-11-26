from pathlib import Path

import pytest

from sheetanalysis.load import edges_on_grid, load_trials
from sheetanalysis.stats import mann_whitney

HEADER = "time_s,inputs,edges,stage,status,file\n"


def write_trace(root: Path, config: str, trial: int, rows: list[str]) -> None:
    directory = root / config / f"trial-{trial}"
    directory.mkdir(parents=True)
    (directory / "covtrace.csv").write_text(HEADER + "".join(f"{row}\n" for row in rows))


def test_load_trials_adds_config_and_trial(tmp_path: Path) -> None:
    write_trace(tmp_path, "B0", 1, ["0.0,1,10,parse,ok,a", "5.0,2,20,resolve,ok,b"])
    write_trace(tmp_path, "G0", 2, ["1.0,1,15,resolve,signal:6,c"])
    trials = load_trials(tmp_path)
    assert trials.height == 3
    assert set(trials["config"]) == {"B0", "G0"}
    assert set(trials["trial"]) == {1, 2}


def test_load_trials_requires_data(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError):
        load_trials(tmp_path)


def test_edges_on_grid_is_a_step_function(tmp_path: Path) -> None:
    write_trace(tmp_path, "B0", 1, ["2.0,1,10,parse,ok,a", "5.0,2,20,resolve,ok,b"])
    sampled = edges_on_grid(load_trials(tmp_path), [0.0, 2.0, 4.0, 5.0, 9.0])
    assert sampled["edges"].to_list() == [0, 10, 10, 20, 20]


def test_mann_whitney_separated_samples() -> None:
    result = mann_whitney([10, 11, 12, 13, 14], [20, 21, 22, 23, 24])
    assert result.p_value < 0.05
    assert result.a_beats_b == 0.0


def test_mann_whitney_identical_samples() -> None:
    result = mann_whitney([5, 6, 7], [5, 6, 7])
    assert result.p_value > 0.5
    assert result.a_beats_b == pytest.approx(0.5)
