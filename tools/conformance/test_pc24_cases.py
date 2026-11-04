"""The PC=24 cases are well formed and actually discriminate (no compiler or 32-bit CPU needed).

At PC=24 and inside float's range the x87 result equals the exact result rounded to float (round to nearest even),
so `f32` stands in for the hardware here; outside that range it does not, which is what the range cases are for.
"""
import math

from tools.conformance import cases_pc24, pc24
from tools.conformance.cases import CASES
from tools.conformance.cases_pc24 import CASES_PC24, PC24, _BO3, _RANGE, _T, f32


def test_well_formed():
    names = [c["name"] for c in CASES_PC24]
    assert len(names) == len(set(names))
    assert not set(names) & {c["name"] for c in CASES}
    for c in CASES_PC24:
        assert c["kind"] == "fpu" and c["tol"] == 0.0
        assert c["asm"][:2] == PC24, c["name"]           # precision set first, as game code does
        assert all(len(v) == 2 for v in c["inputs"])
        assert '"' not in c["why"]                        # goes into a C string literal


def test_bo3_inputs_need_pc24():
    # with PC=24 the product equals its float copy by construction ...
    for a, b in _BO3:
        assert f32(a * b) == f32(f32(a * b))
    # ... and at double precision most of them do not - those are the vectors that catch a model without rounding
    differ = [(a, b) for a, b in _BO3 if a * b != f32(a * b)]
    assert len(differ) >= len(_BO3) - 1
    a, b = _BO3[0]                                        # the case from the race-start freeze
    assert a * b != f32(a * b)


def test_ties_round_to_even():
    assert f32(1.0 + _T) == 1.0
    assert f32(1.0 + 3 * _T) == 1.0 + 4 * _T
    assert f32(16777216.0 + 1.0) == 16777216.0
    assert f32(16777216.0 + 3.0) == 16777220.0


def test_range_inputs_leave_float_range():
    # a model that rounds through (float) turns these into inf or 0 - the x87 at PC=24 does not
    flt_max, flt_min_sub = 3.4028234663852886e38, 2.0 ** -149
    out = [abs(a * b) for a, b in _RANGE]
    assert sum(x > flt_max for x in out) >= 2                 # (float) would give inf
    assert sum(0.0 < x < flt_min_sub / 2 for x in out) >= 1   # (float) would give 0
    assert sum(flt_min_sub <= x < 1.1754943508222875e-38 for x in out) >= 1   # float denormal: fewer bits than 24
    assert not any(math.isinf(x) for x in out)


def test_runner_swaps_case_list(monkeypatch):
    seen = {}

    def fake(argv, allow_container=True):
        seen["argv"] = argv
        seen["cases"] = pc24.runner.CASES
        return 0

    monkeypatch.setattr(pc24.runner, "main_with_args", fake)
    monkeypatch.setattr(pc24.runner, "CASES", pc24.runner.CASES)
    monkeypatch.setattr(pc24.runner, "_WHY", pc24.runner._WHY)
    monkeypatch.setattr(pc24.runner, "_TOL", pc24.runner._TOL)
    assert pc24.main(["-k", "bo3"]) == 0
    assert seen["argv"] == ["--only", "snippets", "-k", "bo3"]
    assert seen["cases"] is cases_pc24.CASES_PC24
