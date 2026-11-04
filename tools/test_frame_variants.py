"""Tests for tools/frame_variants.py (G3 rule 4 against the base variant set, +/-1 movie frame)."""
from tools import frame_variants
from tools.test_frame_compare import _png


def _shot(run, movie, frame, value):
    _png(str(run / f"shot_mv{movie:02d}_f{frame:04d}.png"), 4, 4, lambda x, y: (value, value, value))


def test_head_matches_any_base_variant_or_a_neighbour_frame(tmp_path):
    b1, b2, nb, h = (tmp_path / n for n in ("base_run1", "base_run2", "base_nb1", "head_run1"))
    _shot(b1, 0, 60, 100)
    _shot(b2, 0, 60, 120)                # second base variant of the same capture
    _shot(nb, 0, 61, 140)                # base neighbour k+1
    _shot(h, 0, 60, 120)
    args = ["--base", str(b1), str(b2), "--neighbours", str(nb), "--head", str(h)]
    assert frame_variants.main(args) == 0
    _shot(h, 0, 60, 140)                 # equals only the neighbour frame
    assert frame_variants.main(args) == 0
    _shot(h, 0, 60, 200)                 # nothing like it on the base side
    assert frame_variants.main(args) == 1


def test_late_captures_get_no_verdict_but_a_sheet(tmp_path):
    b1, h = tmp_path / "base_run1", tmp_path / "head_run1"
    _shot(b1, 2, 480, 10)
    _shot(h, 2, 480, 250)
    sheet = tmp_path / "sheet.html"
    assert frame_variants.main(["--base", str(b1), "--head", str(h), "--sheet", str(sheet)]) == 0
    text = sheet.read_text(encoding="utf-8")
    assert "decoded frame 480" in text and text.count("<img") == 2


def test_a_capture_without_base_reference_fails(tmp_path):
    b1, h = tmp_path / "base_run1", tmp_path / "head_run1"
    _shot(b1, 0, 10, 10)
    _shot(h, 0, 60, 10)
    assert frame_variants.main(["--base", str(b1), "--head", str(h)]) == 1


def test_late_capture_identical_to_a_base_variant_passes_without_the_sheet(tmp_path):
    b1, b2, h1, h2 = (tmp_path / n for n in ("base_run1", "base_run2", "head_run1", "head_run2"))
    _shot(b1, 2, 480, 10)
    _shot(b2, 2, 480, 30)                # second base variant
    _shot(h1, 2, 480, 30)                # pixel-identical to it: auto pass
    _shot(h2, 2, 480, 31)                # within the tolerance but not identical: manual sheet
    sheet, out = tmp_path / "sheet.html", tmp_path / "r4.json"
    args = ["--base", str(b1), str(b2), "--head", str(h1), str(h2), "--sheet", str(sheet), "--json", str(out)]
    assert frame_variants.main(args) == 0
    text = sheet.read_text(encoding="utf-8")
    assert "head_run2" in text and "head_run1" not in text
    import json
    r = json.loads(out.read_text(encoding="utf-8"))
    assert r["manual"] == ["shot_mv02_f0480 head_run2"]
    assert {"capture": "shot_mv02_f0480", "head_run": "head_run1", "verdict": "identical"} in r["rows"]
    _shot(h2, 2, 480, 10)                # now identical too: nothing left for the sheet
    assert frame_variants.main(args) == 0
    assert json.loads(out.read_text(encoding="utf-8"))["manual"] == []


def test_captures_the_maintainer_approved_pass_automatically(tmp_path):
    b1, h1, h2 = tmp_path / "base_run1", tmp_path / "head_run1", tmp_path / "head_run2"
    book, sheet = tmp_path / "approved.json", tmp_path / "sheet.html"
    _shot(b1, 2, 480, 10)
    _shot(h1, 2, 480, 250)               # differs from the base: manual check
    args = ["--base", str(b1), "--head", str(h1), "--json", str(tmp_path / "r.json")]
    assert frame_variants.main(args + ["--add-approved", str(book), "--note", "test approval"]) == 0
    _shot(h2, 2, 480, 250)               # a later run, the same picture
    assert frame_variants.main(["--base", str(b1), "--head", str(h2), "--approved", str(book),
                                "--sheet", str(sheet)]) == 0
    assert "<img class=h" not in sheet.read_text(encoding="utf-8")
    _shot(h2, 2, 480, 251)               # one level off: not approved, back to the sheet
    assert frame_variants.main(["--base", str(b1), "--head", str(h2), "--approved", str(book),
                                "--sheet", str(sheet)]) == 0
    assert "<img class=h" in sheet.read_text(encoding="utf-8")
