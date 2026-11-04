"""xbe2pe on a synthetic XBE (no game data): the flat PE puts every section at its VA, keeps the entry point and
resolves every exported name to its address the way a PE loader does (sorted name table -> ordinal -> RVA)."""
import json
import struct

import pytest

from tools.symbols.xbe2pe import DEBUG_ENTRY_KEY, RETAIL_ENTRY_KEY, build_pe, flat_image, load_names, main

BASE = 0x10000
TEXT = (0x11000, bytes(range(256)) * 15 + b'\xC3' * 0x100)      # va, data (vs = rs, within one raw page)
DATA = (0x13000, b'DATA' * 0x80)


def make_xbe(entry_key=RETAIL_ENTRY_KEY, entry=0x11010):
    hsize, isize = 0x400, 0x4000
    x = bytearray(0x3000)
    x[:4] = b'XBEH'
    struct.pack_into('<III', x, 0x104, BASE, hsize, isize)
    struct.pack_into('<II', x, 0x11C, 2, BASE + 0x200)          # section count, section headers VA
    struct.pack_into('<I', x, 0x128, entry ^ entry_key)
    raw = 0x1000
    for i, (va, data) in enumerate((TEXT, DATA)):
        struct.pack_into('<IIIII', x, 0x200 + i * 56, 6, va, len(data), raw, len(data))
        x[raw:raw + len(data)] = data
        raw += 0x1000
    return bytes(x)


def parse_pe(pe):
    u16 = lambda o: struct.unpack_from('<H', pe, o)[0]
    u32 = lambda o: struct.unpack_from('<I', pe, o)[0]
    assert pe[:2] == b'MZ'
    nt = u32(0x3C)
    assert pe[nt:nt + 4] == b'PE\0\0' and u16(nt + 4) == 0x14C
    opt = nt + 24
    assert u16(opt) == 0x10B
    entry_rva, image_base = u32(opt + 16), u32(opt + 28)
    exp_rva = u32(opt + 96)
    nsec, optsize = u16(nt + 6), u16(nt + 20)
    secs = []
    for i in range(nsec):
        s = opt + optsize + 40 * i
        name = pe[s:s + 8].rstrip(b'\0').decode()
        vs, va, rs, ra = struct.unpack_from('<IIII', pe, s + 8)
        secs.append((name, va, vs, ra, rs))
    rva2off = lambda r: next(ra + r - va for _, va, vs, ra, rs in secs if va <= r < va + rs)
    e = rva2off(exp_rva)
    n_names, eat, npt, ot = u32(e + 24), u32(e + 28), u32(e + 32), u32(e + 36)
    names = []
    for i in range(n_names):
        so = rva2off(u32(rva2off(npt) + 4 * i))
        nm = pe[so:pe.index(b'\0', so)].decode()
        ordinal = u16(rva2off(ot) + 2 * i)
        names.append((nm, image_base + u32(rva2off(eat) + 4 * ordinal)))
    return image_base, image_base + entry_rva, secs, rva2off, names


def test_sections_at_their_va_and_entry_point():
    base, img, entry = flat_image(make_xbe())
    pe, n = build_pe(base, img, entry, {})
    image_base, ep, secs, rva2off, names = parse_pe(pe)
    assert (image_base, ep, n, names) == (BASE, 0x11010, 0, [])
    for va, data in (TEXT, DATA):
        o = rva2off(va - BASE)
        assert pe[o:o + len(data)] == data


def test_debug_key_entry():
    base, img, entry = flat_image(make_xbe(DEBUG_ENTRY_KEY, 0x11234))
    assert entry == 0x11234


def test_exports_resolve_sorted_and_in_range():
    names = {0x11200: 'zeta', 0x11010: 'Alpha', 0x13004: 'g_table', 0x11100: 'mid',
             0x10100: 'in_headers', 0x99999: 'outside'}
    pe, n = build_pe(*flat_image(make_xbe())[:2], 0x11010, names)
    *_, got = parse_pe(pe)
    assert n == 4
    assert [g[0] for g in got] == sorted(g[0] for g in got)              # binary-searchable
    assert dict(got) == {'Alpha': 0x11010, 'mid': 0x11100, 'zeta': 0x11200, 'g_table': 0x13004}


def test_names_from_functions_json_and_override(tmp_path):
    fj = tmp_path / 'functions.json'
    fj.write_text(json.dumps([{'start': '0x00011010', 'end': '0x00011020', 'name': 'sub_00011010'},
                              {'start': '0x00011100', 'end': '0x00011110', 'name': 'Cls::Method'},
                              {'start': '0x00011200', 'end': '0x00011210'}]))
    nj = tmp_path / 'names.json'
    nj.write_text(json.dumps({'0x00011010': 'main', '0x00011300': 'memcpy', '0x00011400': ''}))
    assert load_names([fj], [nj]) == {0x11010: 'main', 0x11100: 'Cls_Method', 0x11200: 'sub_00011200',
                                      0x11300: 'memcpy'}
    xbe = tmp_path / 'default.xbe'
    xbe.write_bytes(make_xbe())
    out = tmp_path / 'image.exe'
    assert main([str(xbe), str(out), '--functions', str(fj), '--names', str(nj)]) == 0
    *_, got = parse_pe(out.read_bytes())
    assert dict(got)['memcpy'] == 0x11300 and dict(got)['main'] == 0x11010


def test_rejects_non_xbe():
    with pytest.raises(ValueError):
        flat_image(b'MZ' + bytes(0x200))
