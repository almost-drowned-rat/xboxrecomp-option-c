"""Flat PE of an XBE for Ghidra/IDA: the image at its load address, one section, function names as exports.

    py -3 -m tools.symbols.xbe2pe <default.xbe> <out.exe> [--functions dis/functions.json] [--names names.json]

Every section's bytes sit at their virtual address (like the flattening of tools/ghidra_naming), the entry point is
the XBE's (retail or debug key), and every named address becomes an export, so a loader that knows PE needs no XBE
plugin and shows the lifter's function names. Names: `functions.json` from tools.disasm (its `name`, after any
merge_names.py --apply), then `--names` (a JSON object {"0xVA": "name"}) on top; `::` becomes `_`.

Adapted from TGP482/XboxXBE-Recomp tools/symbols/xbe2pe.py (commit 65e959d), which reads a game TOML and the
recompiler's x86_func_mapping.cpp instead. Original licence:

    MIT License

    Copyright (c) 2026 TGP482

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

RETAIL_ENTRY_KEY = 0xA8FC57AB
DEBUG_ENTRY_KEY = 0x94859D4B
PAGE = 0x1000


def flat_image(xbe: bytes):
    """(base, image bytes, entry VA): every section at its virtual address, headers at the base."""
    u32 = lambda o: struct.unpack_from('<I', xbe, o)[0]
    if xbe[:4] != b'XBEH':
        raise ValueError('not an XBE (no XBEH magic)')
    base, hsize, isize = u32(0x104), u32(0x108), u32(0x10C)
    entry = u32(0x128) ^ RETAIL_ENTRY_KEY
    if not base <= entry < base + isize:
        entry = u32(0x128) ^ DEBUG_ENTRY_KEY
    img = bytearray(isize)
    img[:hsize] = xbe[:hsize]
    # section headers are 56 bytes: flags, virtual address, virtual size, raw address, raw size, ...
    for i in range(u32(0x11C)):
        va, vs, raw, rs = struct.unpack_from('<IIII', xbe, u32(0x120) - base + i * 56 + 4)
        n = min(vs, rs)
        img[va - base:va - base + n] = xbe[raw:raw + n]
    return base, bytes(img), entry


def load_names(functions=(), names=()):
    """{va: name} from disasm functions.json files, then {"0xVA": name} JSON maps on top."""
    out = {}
    for p in functions:
        for f in json.loads(Path(p).read_text(encoding='utf-8')):
            va = int(f['start'], 16)
            out[va] = f.get('name') or 'sub_%08X' % va
    for p in names:
        for k, v in json.loads(Path(p).read_text(encoding='utf-8')).items():
            if v:
                out[int(k, 0)] = v
    return {va: re.sub(r'[^0-9A-Za-z_?@$.]', '_', n.replace('::', '_')) for va, n in out.items()}


def build_pe(base: int, img: bytes, entry: int, names: dict):
    """(PE file bytes, number of exports); names outside the image or in its first page are left out."""
    isize = len(img)
    exp = sorted(va for va in names if base + PAGE <= va < base + isize)
    nm = [names[a].encode() for a in exp]
    order = sorted(range(len(exp)), key=lambda i: nm[i])        # name pointer table must be sorted (binary search)
    # export directory (40 bytes), address table, name pointers, ordinals and names, page aligned after the image
    ert = (isize + PAGE - 1) & ~(PAGE - 1)
    eat = ert + 40
    npt = eat + 4 * len(exp)
    ot = npt + 4 * len(exp)
    st = ot + 2 * len(exp)
    strs, off = bytearray(b'image.exe\0'), {}
    for i in range(len(exp)):
        off[i] = st + len(strs)
        strs += nm[i] + b'\0'
    edata = struct.pack('<IIHHIIIIIII', 0, 0, 0, 0, st, 1, len(exp), len(exp), eat, npt, ot)
    edata += b''.join(struct.pack('<I', a - base) for a in exp)
    edata += b''.join(struct.pack('<I', off[i]) for i in order)
    edata += b''.join(struct.pack('<H', i) for i in order)
    edata += strs
    esz = (len(edata) + PAGE - 1) & ~(PAGE - 1)

    # PE32 optional header: entry RVA, image base, section/file alignment 0x1000, subsystem 2, 16 data directories
    opt = struct.pack('<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII', 0x10B, 14, 0, isize, 0, 0, entry - base, PAGE, PAGE, base,
                      PAGE, PAGE, 4, 0, 0, 0, 4, 0, 0, ert + esz, PAGE, 0, 2, 0, 0x100000, PAGE, 0x100000, PAGE,
                      0, 16)
    opt += struct.pack('<II', ert, len(edata)) + bytes(8 * 15)
    coff = struct.pack('<HHIIIHH', 0x14C, 2, 0, 0, 0, len(opt), 0x0102)
    flat = struct.pack('<8sIIIIIIHHI', b'.flat', isize - PAGE, PAGE, isize - PAGE, PAGE, 0, 0, 0, 0, 0xE0000060)
    exps = struct.pack('<8sIIIIIIHHI', b'.edata', len(edata), ert, esz, ert, 0, 0, 0, 0, 0x40000040)
    dos = bytearray(0x40)
    dos[:2] = b'MZ'
    struct.pack_into('<I', dos, 0x3C, 0x40)
    head = bytes(dos) + b'PE\0\0' + coff + opt + flat + exps

    f = bytearray(ert + esz)
    f[PAGE:isize] = img[PAGE:]
    f[:len(head)] = head
    f[ert:ert + len(edata)] = edata
    return bytes(f), len(exp)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('xbe')
    ap.add_argument('out')
    ap.add_argument('--functions', action='append', default=[], help='tools.disasm functions.json (repeatable)')
    ap.add_argument('--names', action='append', default=[], help='JSON {"0xVA": "name"} applied on top (repeatable)')
    a = ap.parse_args(argv)
    base, img, entry = flat_image(Path(a.xbe).read_bytes())
    pe, n = build_pe(base, img, entry, load_names(a.functions, a.names))
    Path(a.out).write_bytes(pe)
    print(a.out, n, 'exports')
    return 0


if __name__ == '__main__':
    sys.exit(main())
