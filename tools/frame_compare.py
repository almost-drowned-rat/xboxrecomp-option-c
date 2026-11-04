"""Content compare of frame dumps between two runs (G3 rule 4, docs/option_c.md).

    python tools/frame_compare.py <base run dir> <head run dir> [--masks masks.json] [--tol 8]
                                  [--max-diff 0.1] [--glob dump/d*.png] [--json out.json]

Compares the PNGs matching --glob in both run dirs (default dump/d*.png, ref_run.ps1 -DumpEvery; G3 uses
shot_mv*.png, the decoded-movie-frame captures, since movies run on real time and dump N shows a different
movie frame in every run). Frames with the same name are compared:
a pixel differs when any channel differs by more than --tol (the per-pixel tolerance, 0-255); pixels
inside a mask rectangle are not compared. A frame matches when at most --max-diff percent of its
unmasked pixels differ. Frames present in only one run are listed, not compared.

Masks (json): {"tol": 8, "max_diff_pct": 0.1, "frames": {"*": [rects], "d00060": [rects]},
"reasons": {"d00060": "..."}} with rect = [x, y, w, h] in pixels. Rule 4 limits: at most 10 % of a
frame masked, above 5 % only with a written reason - both checked here (a frame over the limit is a
failure, not a warning). Masks are frozen once committed; change them only with the maintainer's OK.

Standard library only (zlib PNG reader: 8-bit RGB/RGBA, non-interlaced - what System.Drawing writes).
Exit code 0 = every common frame matches, 1 = a mismatch or a mask over the limit, 2 = usage error.
"""
import argparse
import glob
import json
import os
import struct
import sys
import zlib

MASK_LIMIT_PCT = 10.0
MASK_REASON_PCT = 5.0


def read_png(path):
    """Return (width, height, channels, bytes) with the filters undone."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, hdr = 8, [], None
    while pos < len(data):
        n, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    w, h, depth, ctype, _, _, interlace = hdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError(f"{path}: only 8-bit RGB/RGBA non-interlaced PNGs (got depth {depth}, type {ctype})")
    ch = 3 if ctype == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    stride = w * ch
    out = bytearray(stride * h)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif f != 0:
            raise ValueError(f"{path}: bad filter {f} in row {y}")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, ch, bytes(out)


def mask_grid(w, h, rects):
    """Per row a bytearray (1 = masked pixel), and the masked pixel count; overlapping rects count once."""
    covered = bytearray(w * h)
    for x, y, rw, rh in rects:
        x0, x1 = max(0, x), min(w, x + rw)
        if x1 <= x0:
            continue
        for yy in range(max(0, y), min(h, y + rh)):
            covered[yy * w + x0:yy * w + x1] = b"\x01" * (x1 - x0)
    return [covered[yy * w:(yy + 1) * w] for yy in range(h)], sum(covered)


def compare(a, b, rects, tol):
    """Percent of unmasked pixels whose channels differ by more than tol; masked percent."""
    wa, ha, ca, pa = a
    wb, hb, cb, pb = b
    if (wa, ha) != (wb, hb):
        return 100.0, 0.0, "size differs"
    rows, masked = mask_grid(wa, ha, rects)
    diff = 0
    for y in range(ha):
        mrow = rows[y]
        ra = pa[y * wa * ca:(y + 1) * wa * ca]
        rb = pb[y * wb * cb:(y + 1) * wb * cb]
        if ra == rb:
            continue
        for x in range(wa):
            if mrow[x]:
                continue
            ia, ib = x * ca, x * cb
            if (abs(ra[ia] - rb[ib]) > tol or abs(ra[ia + 1] - rb[ib + 1]) > tol
                    or abs(ra[ia + 2] - rb[ib + 2]) > tol):
                diff += 1
    total = wa * ha
    unmasked = total - masked
    return (100.0 * diff / unmasked if unmasked else 0.0), 100.0 * masked / total, ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("base")
    ap.add_argument("head")
    ap.add_argument("--masks")
    ap.add_argument("--tol", type=int)
    ap.add_argument("--max-diff", type=float, help="percent of unmasked pixels allowed to differ")
    ap.add_argument("--glob", default="dump/d*.png", help="PNGs to compare, relative to each run dir")
    ap.add_argument("--json")
    args = ap.parse_args(argv)

    cfg = json.load(open(args.masks, encoding="utf-8")) if args.masks else {}
    tol = args.tol if args.tol is not None else cfg.get("tol", 8)
    max_diff = args.max_diff if args.max_diff is not None else cfg.get("max_diff_pct", 0.1)
    frames_cfg = cfg.get("frames", {})
    reasons = cfg.get("reasons", {})

    if not os.path.isdir(args.base) or not os.path.isdir(args.head):
        print("a run dir is missing", file=sys.stderr)
        return 2

    def frames(run):
        return {os.path.basename(f)[:-4]: f for f in glob.glob(os.path.join(run, args.glob))}

    fa, fb = frames(args.base), frames(args.head)
    na, nb = set(fa), set(fb)
    common = sorted(na & nb)
    result = {"base": args.base, "head": args.head, "tol": tol, "max_diff_pct": max_diff,
              "only_base": sorted(na - nb), "only_head": sorted(nb - na), "frames": []}
    ok = True
    for name in common:
        rects = frames_cfg.get("*", []) + frames_cfg.get(name, [])
        pct, masked, note = compare(read_png(fa[name]), read_png(fb[name]), rects, tol)
        verdict = "match" if pct <= max_diff else "MISMATCH"
        if masked > MASK_LIMIT_PCT:
            verdict, note = "MASK OVER LIMIT", f"{masked:.1f} % masked > {MASK_LIMIT_PCT} %"
        elif masked > MASK_REASON_PCT and not reasons.get(name):
            verdict, note = "MASK NEEDS REASON", f"{masked:.1f} % masked > {MASK_REASON_PCT} % without a reason"
        ok = ok and verdict == "match"
        result["frames"].append({"frame": name, "diff_pct": round(pct, 4), "masked_pct": round(masked, 2),
                                 "verdict": verdict, "note": note})
        print(f"{name}  diff {pct:7.3f} %  masked {masked:5.1f} %  {verdict} {note}".rstrip())
    if result["only_base"] or result["only_head"]:
        print(f"only in base: {' '.join(result['only_base']) or '-'}; only in head: {' '.join(result['only_head']) or '-'}")
    if not common:
        ok = False
    print(f"{'PASS' if ok else 'FAIL'}: {len(common)} frames compared, tol {tol}, max diff {max_diff} %")
    result["verdict"] = "pass" if ok else "fail"
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(result, f, indent=1)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
