import random
from pathlib import Path

import pytest

from sheetgen import cli


def fake_generator(rng: random.Random, *, max_depth: int = 20) -> str:
    return f"#sheet v1\nA1 = {rng.randint(0, 10**6)}\n# depth {max_depth}\n"


@pytest.fixture
def fake(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setitem(cli.GENERATORS, "naive", fake_generator)  # type: ignore[index]


@pytest.mark.usefixtures("fake")
def test_writes_numbered_files(tmp_path: Path) -> None:
    assert cli.main(["naive", "--count", "3", "--seed", "1", "--out", str(tmp_path)]) == 0
    assert sorted(p.name for p in tmp_path.iterdir()) == [
        "00000000.sheet",
        "00000001.sheet",
        "00000002.sheet",
    ]


@pytest.mark.usefixtures("fake")
def test_same_seed_reproduces_the_corpus(tmp_path: Path) -> None:
    first, second, other = tmp_path / "a", tmp_path / "b", tmp_path / "c"
    cli.main(["naive", "--count", "5", "--seed", "7", "--out", str(first)])
    cli.main(["naive", "--count", "5", "--seed", "7", "--out", str(second)])
    cli.main(["naive", "--count", "5", "--seed", "8", "--out", str(other)])

    def contents(directory: Path) -> list[str]:
        return [p.read_text() for p in sorted(directory.iterdir())]

    assert contents(first) == contents(second)
    assert contents(first) != contents(other)


@pytest.mark.usefixtures("fake")
def test_max_depth_is_passed_through(tmp_path: Path) -> None:
    cli.main(["naive", "--count", "1", "--seed", "1", "--out", str(tmp_path), "--max-depth", "9"])
    assert "# depth 9" in (tmp_path / "00000000.sheet").read_text()
