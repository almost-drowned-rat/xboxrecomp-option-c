"""Tests for tools/profile_calibration.py (cross-profile calibration of a G3 reference)."""
import json

from tools import profile_calibration as pc


def _run(path, englis_ms, femain_ms, private_mb):
    path.mkdir()
    (path / "summary.json").write_text(json.dumps({
        "first_open_ms": {"D:\\ovid\\englis30.xmv": englis_ms, "D:\\tracks\\_femain.rws": femain_ms},
        "avg_cpu_percent": 92.0, "peak_private_mb": private_mb, "peak_working_set_mb": 370.0}), encoding="utf-8")


def test_verdicts():
    a = {"n": 5, "mean": 10.0, "sd": 0.05, "range": 0.1}
    assert pc.compare(a, {"n": 5, "mean": 10.05, "sd": 0.05, "range": 0.1})["verdict"] == "equivalent"
    assert pc.compare(a, {"n": 5, "mean": 10.5, "sd": 0.05, "range": 0.1})["verdict"] == "corrected"
    # an offset known worse than two runs of one profile differ: not transferred
    assert pc.compare(a, {"n": 5, "mean": 10.5, "sd": 0.5, "range": 0.1})["verdict"] == "unstable"


def test_additive_offset_and_zero_ram(tmp_path, capsys):
    a = [tmp_path / f"a{i}" for i in range(5)]
    b = [tmp_path / f"b{i}" for i in range(5)]
    for i, d in enumerate(a):
        _run(d, 4000 + 20 * i, 74000 + 20 * i, 406.0 + 0.2 * i)
    for i, d in enumerate(b):                      # +0.3 s on every milestone, same RAM
        _run(d, 4300 + 20 * i, 74300 + 20 * i, 406.0 + 0.2 * i)
    out = tmp_path / "cal.json"
    argv = ["x", "--a", "PANEL1", *map(str, a), "--b", "PANEL2", *map(str, b),
            "--milestone", "englis=englis30.xmv", "--milestone", "femain=_femain.rws", "--json", str(out)]
    import sys
    old, sys.argv = sys.argv, argv
    try:
        assert pc.main() == 0
    finally:
        sys.argv = old
    cal = json.loads(out.read_text(encoding="utf-8"))
    assert cal["equivalent"] is False
    assert cal["milestone_shape"]["better"] == "additive"
    assert abs(cal["milestone_shape"]["additive_offset_s"] - 0.3) < 1e-6
    ram = next(m for m in cal["metrics"] if m["metric"] == "private MB")
    assert ram["cmp"]["verdict"] == "equivalent" and abs(ram["cmp"]["offset"]) < 1e-9
    assert "zero within A/A noise" in capsys.readouterr().out
