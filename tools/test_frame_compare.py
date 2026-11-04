"""Tests for tools/frame_compare.py (G3 rule 4 content compare)."""
import json
import os
import struct
import zlib

from tools import frame_compare


def _png(path, w, h, pixel, filt=0):
    """Write an 8-bit RGB PNG; pixel(x, y) -> (r, g, b); every row uses filter `filt` (0 = none, 1 = sub)."""
    rows = []
    for y in range(h):
        line = bytearray()
        for x in range(w):
            line += bytes(pixel(x, y))
        if filt == 1:
            enc = bytearray(line[:3]) + bytearray((line[i] - line[i - 3]) & 0xFF for i in range(3, len(line)))
        else:
            enc = line
        rows.append(bytes([filt]) + bytes(enc))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def _gradient(x, y):
    return (x * 7 & 0xFF, y * 5 & 0xFF, (x + y) & 0xFF)


def test_reader_undoes_the_sub_filter(tmp_path):
    _png(str(tmp_path / "a.png"), 20, 10, _gradient, filt=1)
    w, h, ch, px = frame_compare.read_png(str(tmp_path / "a.png"))
    assert (w, h, ch) == (20, 10, 3)
    assert tuple(px[(3 * 20 + 4) * 3:(3 * 20 + 4) * 3 + 3]) == _gradient(4, 3)


def test_identical_and_within_tolerance_frames_match(tmp_path):
    _png(str(tmp_path / "b/dump/d00060.png"), 20, 10, _gradient)
    _png(str(tmp_path / "h/dump/d00060.png"), 20, 10, lambda x, y: tuple(min(255, c + 3) for c in _gradient(x, y)))
    assert frame_compare.main([str(tmp_path / "b"), str(tmp_path / "h"), "--tol", "4", "--max-diff", "0"]) == 0
    assert frame_compare.main([str(tmp_path / "b"), str(tmp_path / "h"), "--tol", "2", "--max-diff", "0"]) == 1


def test_a_mask_hides_a_difference_and_its_size_is_limited(tmp_path):
    _png(str(tmp_path / "b/dump/d00060.png"), 20, 10, _gradient)
    _png(str(tmp_path / "h/dump/d00060.png"), 20, 10,
         lambda x, y: (255, 255, 255) if x < 2 and y < 2 else _gradient(x, y))
    base, head = str(tmp_path / "b"), str(tmp_path / "h")
    assert frame_compare.main([base, head, "--max-diff", "0"]) == 1
    small = tmp_path / "small.json"          # 4 of 200 pixels = 2 %: allowed without a reason
    small.write_text(json.dumps({"frames": {"d00060": [[0, 0, 2, 2]]}}))
    assert frame_compare.main([base, head, "--masks", str(small), "--max-diff", "0"]) == 0
    mid = tmp_path / "mid.json"              # 8 % needs a reason
    mid.write_text(json.dumps({"frames": {"d00060": [[0, 0, 4, 4]]}}))
    assert frame_compare.main([base, head, "--masks", str(mid), "--max-diff", "0"]) == 1
    mid.write_text(json.dumps({"frames": {"d00060": [[0, 0, 4, 4]]}, "reasons": {"d00060": "clock"}}))
    assert frame_compare.main([base, head, "--masks", str(mid), "--max-diff", "0"]) == 0
    big = tmp_path / "big.json"              # 20 % is over the 10 % limit even with a reason
    big.write_text(json.dumps({"frames": {"*": [[0, 0, 20, 2]]}, "reasons": {"d00060": "x"}}))
    assert frame_compare.main([base, head, "--masks", str(big), "--max-diff", "0"]) == 1


def test_frames_in_one_run_only_are_listed_not_compared(tmp_path):
    _png(str(tmp_path / "b/dump/d00060.png"), 4, 4, _gradient)
    _png(str(tmp_path / "b/dump/d00120.png"), 4, 4, _gradient)
    _png(str(tmp_path / "h/dump/d00060.png"), 4, 4, _gradient)
    out = tmp_path / "r.json"
    assert frame_compare.main([str(tmp_path / "b"), str(tmp_path / "h"), "--json", str(out)]) == 0
    r = json.loads(out.read_text())
    assert r["only_base"] == ["d00120"] and len(r["frames"]) == 1


def test_glob_selects_the_movie_captures_and_nothing_common_fails(tmp_path):
    _png(str(tmp_path / "b/shot_mv00_f0010.png"), 4, 4, _gradient)
    _png(str(tmp_path / "h/shot_mv00_f0010.png"), 4, 4, _gradient)
    _png(str(tmp_path / "h/shot_t10.png"), 4, 4, lambda x, y: (0, 0, 0))
    base, head = str(tmp_path / "b"), str(tmp_path / "h")
    assert frame_compare.main([base, head, "--glob", "shot_mv*.png"]) == 0
    assert frame_compare.main([base, head]) == 1          # no dump/ frames at all: nothing compared is a failure
