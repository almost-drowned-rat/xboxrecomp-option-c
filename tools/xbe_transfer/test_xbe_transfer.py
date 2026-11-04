"""Unit tests on synthetic images (no game data): a source build and a target build where the code moved by 0x40
and the data by 0x300, one function changed, one removed, two identical ones."""
import json
import struct

from tools.xbe_transfer.image import Image, Section
from tools.xbe_transfer.mask import signature
from tools.xbe_transfer.match import Func, build_map
from tools.xbe_transfer.remap import remap_json, remap_text
from tools.xbe_transfer.__main__ import load, save

TEXT, DATA = 0x11000, 0x20000
CODE_MOVE, DATA_MOVE = 0x40, 0x300
D1, D2, D3, D4, D5 = 0x20010, 0x20020, 0x20030, 0x20040, 0x20050      # D4: named by no code
SLOT = {"f1": 0x11000, "f2": 0x11020, "f3": 0x11040, "f4": 0x11060, "f5": 0x11080,
        "f6": 0x110A0, "f7": 0x110C0, "f8": 0x110E0, "f9": 0x11100}


def u32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def build(target: bool):
    cm, dm = (CODE_MOVE, DATA_MOVE) if target else (0, 0)
    at = {k: v + cm for k, v in SLOT.items()}
    d = lambda a: u32(a + dm)

    def call(src_va, name):              # E8 rel32, src_va = address of the E8 byte
        return b"\xE8" + u32(at[name] - (src_va + 5))

    body = {}
    a = at["f1"]                          # push ebp; mov ebp,esp; mov eax,[D1]; call f2; call f5; pop ebp; ret
    body["f1"] = b"\x55\x8B\xEC\xA1" + d(D1) + call(a + 8, "f2") + call(a + 13, "f5") + b"\x5D\xC3"
    body["f2"] = b"\x8B\x0D" + d(D2) + b"\x03\xC1\xC3"                   # mov ecx,[D2]; add eax,ecx; ret
    body["f3"] = b"\xC3"                                                  # too short to match alone
    a = at["f4"]                          # mov eax,5; call f3 (backwards); call f7; push D3; ret
    body["f4"] = b"\xB8\x05\x00\x00\x00" + call(a + 5, "f3") + call(a + 10, "f7") + b"\x68" + d(D3) + b"\xC3"
    body["f5"] = b"\xB8" + (b"\x02" if target else b"\x01") + b"\x00\x00\x00\xC3"   # changed between builds
    body["f6"] = b"\x90" * 6 if target else b"\x33\xC0\x40\x40\x40\xC3"            # removed in the target
    body["f7"] = b"\x8B\x0D" + d(D5) + b"\xC3"                           # f7 and f8 identical
    body["f8"] = b"\x8B\x0D" + d(D5) + b"\xC3"
    body["f9"] = b"\x31\xD2\x8B\x15" + d(D1) + b"\xC3"
    text = bytearray(b"\xCC" * (0x120 + cm))
    for k, b in body.items():
        text[at[k] - TEXT: at[k] - TEXT + len(b)] = b
    img = Image([Section(".text", TEXT, bytes(text), True), Section(".data", DATA + dm, bytes(0x100), False)])
    sizes = {k: len(b) for k, b in body.items()}
    return img, sizes


def funcs(sizes):
    return [Func(SLOT[k], sizes[k], ".text", k) for k in SLOT]


def make_map():
    src, sizes = build(False)
    dst, _ = build(True)
    return build_map(src, funcs(sizes), dst), src, sizes


def test_signature_masks_address_operands_only():
    src, sizes = build(False)
    sig = signature(src, SLOT["f4"], sizes["f4"])
    kinds = sorted((s.kind, s.value) for s in sig.slots)
    assert kinds == [("abs", D3), ("rel", SLOT["f3"]), ("rel", SLOT["f7"])]
    assert sig.data[1:5] == b"\x05\x00\x00\x00" and all(sig.mask[1:5])   # an immediate that is no address stays


def test_methods():
    m, _, _ = make_map()
    got = {k: (m.functions[SLOT[k]].dst - SLOT[k], m.functions[SLOT[k]].method) for k in SLOT if SLOT[k] in m.functions}
    assert got["f1"] == (CODE_MOVE, "exact")
    assert got["f2"] == (CODE_MOVE, "exact")
    assert got["f4"] == (CODE_MOVE, "exact")
    assert got["f9"] == (CODE_MOVE, "exact")
    assert got["f3"] == (CODE_MOVE, "ref")            # 1 byte, placed by f4's call
    assert got["f7"] == (CODE_MOVE, "ref")            # ambiguous with f8, placed by f4's call
    assert got["f8"] == (CODE_MOVE, "neighbor")       # f7 and f9 moved by the same amount
    assert got["f5"] == (CODE_MOVE, "ref-changed")    # f1 still calls it, the body differs
    assert SLOT["f6"] not in m.functions
    assert "not found" in m.unmatched[SLOT["f6"]]


def test_data_votes_and_translate():
    m, _, _ = make_map()
    assert m.data[D1].dst == D1 + DATA_MOVE and m.data[D1].votes == 2     # f1 and f9
    for a in (D2, D3, D5):
        assert m.data[a].dst == a + DATA_MOVE
    assert m.translate(SLOT["f1"] + 3) == (SLOT["f1"] + CODE_MOVE + 3, "inside/exact")
    assert m.translate(SLOT["f5"] + 1) is None                           # inside a changed body: not carried
    assert m.translate(D4) == (D4 + DATA_MOVE, "data-between")
    assert m.translate(0x20060) is None                                  # past the last known data address


def test_remap_text_conservative():
    m, _, _ = make_map()
    src = ("void sub_00011020(void);  /* override */\n"
           "#define G 0x00020010\n"
           "x = 0x00020040; y = 0x00020060; size = 0x00100000; small = 0x20;\n")
    r = remap_text(m, src, "manual.c")
    assert "sub_00011060" in r.text
    assert "0x00020310" in r.text
    assert "0x00020040" in r.text and "0x00020060" in r.text            # between / unknown: left alone
    assert "0x00100000" in r.text                                       # outside the image: not touched, not reported
    left = sorted(c.old for c in r.changes if c.new is None)
    assert left == [0x20040, 0x20060]
    r2 = remap_text(m, src, "manual.c", interpolate=True)
    assert "0x00020340" in r2.text


def test_slots_pass_places_identical_initialisers():
    """g1 and g2 are the same once masked (mov eax,[const]; mov [global],eax; ret), named by no code, and swapped
    in the target, so neither the neighbour nor the ref pass places them; u (found exactly) names their data."""
    C1, C2, G1, G2 = 0x20010, 0x20014, 0x20020, 0x20024

    def img(target):
        dm = DATA_MOVE if target else 0
        d = lambda a: u32(a + dm)
        g = lambda c, gl: b"\xA1" + d(c) + b"\xA3" + d(gl) + b"\xC3"
        u = b"\x55\xA1" + d(C1) + b"\x03\x05" + d(C2) + b"\x89\x05" + d(G1) + b"\x89\x05" + d(G2) + b"\x5D\xC3"
        text = bytearray(b"\xCC" * 0x60)
        first, second = (g(C2, G2), g(C1, G1)) if target else (g(C1, G1), g(C2, G2))
        text[0x00:0x0B], text[0x20:0x2B], text[0x40:0x40 + len(u)] = first, second, u
        return Image([Section(".text", TEXT, bytes(text), True), Section(".data", DATA + dm, bytes(0x100), False)]), len(u)

    src, ulen = img(False)
    dst, _ = img(True)
    m = build_map(src, [Func(TEXT, 11, ".text", "g1"), Func(TEXT + 0x20, 11, ".text", "g2"),
                        Func(TEXT + 0x40, ulen, ".text", "u")], dst)
    assert m.functions[TEXT + 0x40].method == "exact"
    assert (m.functions[TEXT].dst, m.functions[TEXT].method) == (TEXT + 0x20, "slots")
    assert (m.functions[TEXT + 0x20].dst, m.functions[TEXT + 0x20].method) == (TEXT, "slots")
    assert not m.unmatched


def test_remap_text_suffixes():
    """A wrapper's generated body (`sub_X_gen`) and C literals with an integer suffix are rewritten too."""
    m, _, _ = make_map()
    src = ("extern void sub_00011020_gen(void);\n"
           "#define G 0x00020010u\n"
           "x = 0x00020010UL; y = sub_00011020x; z = 0x00020010z;\n")
    r = remap_text(m, src, "manual.c")
    assert "sub_00011060_gen(" in r.text
    assert "#define G 0x00020310u" in r.text
    assert "x = 0x00020310UL;" in r.text
    assert "sub_00011020x" in r.text and "0x00020010z" in r.text        # not an address token: untouched


def test_remap_json_and_roundtrip(tmp_path):
    m, _, _ = make_map()
    p = tmp_path / "map.json"
    save(m, str(p), {})
    m2 = load(str(p))
    seeds = [{"start": "0x00011040", "note": "f3"}, {"start": "0x000110A0", "note": "f6 (removed)"}]
    out, changes = remap_json(m2, seeds, "seeds.json")
    assert out[0]["start"] == "0x00011080"
    assert out[1]["start"] == "0x000110A0"
    assert [c.new is None for c in changes] == [False, True]
    json.dumps(out)
