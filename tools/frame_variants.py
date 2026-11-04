"""G3 rule 4 against a variant set (docs/option_c.md §1, precise form of 2026-10-24).

    python tools/frame_variants.py --base <run> [<run> ...] --head <run> [<run> ...] [--neighbours <run> ...]
                                   [--auto-upto 120] [--tol 8] [--max-diff 0.1] [--sheet sheet.html] [--json out.json]

Captures are shot_mv<i>_f<k>.png (decoded frame k of the i-th movie, taken at the next flip). The reference for a
capture is the set of all its variants in the clean base runs: the timed --base runs and the --neighbours runs
(also clean base runs, not timed, taken with the bench's RECOMP_G3_MOVIE_SHOTS=<list> for frames k-1, k, k+1). Up to
--auto-upto (k <= that) a head capture passes if it equals a base variant of frame k (frame_compare's tolerance and
max-diff) or a base capture of frame k-1 or k+1 of the same movie: known flip jitter, the same on base and head. Above
--auto-upto a head capture that is pixel-identical to a base variant of frame k passes automatically (maintainer,
2026-10-24); only the others get no verdict: --sheet writes a side-by-side HTML page (every base variant next to each
such head capture) for the manual visual check. The sheet references the PNGs
by path - keep it next to the private results, never in a repo. No masks.

Captures the maintainer has already approved pass automatically too (maintainer, 2026-10-24): --approved <file>
is a JSON list of their pixel hashes per capture name, kept with the reference; a head capture above --auto-upto
whose pixels hash to an approved one gets the verdict "approved". After the maintainer has approved a sheet,
--add-approved <file> records that run's manual captures in the list (with the series and date given by --note).
Exit code 0 = every automatic capture passes, 1 = a failure, 2 = usage error.
"""
import argparse
import glob
import hashlib
import html
import json
import os
import re
import sys

try:
    from tools import frame_compare as fc
except ImportError:                      # run as a script (python -I tools/frame_variants.py)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import frame_compare as fc

NAME = re.compile(r"^shot_mv(\d+)_f(\d+)$")


def captures(run):
    out = {}
    for f in glob.glob(os.path.join(run, "shot_mv*.png")):
        m = NAME.match(os.path.basename(f)[:-4])
        if m:
            out[(int(m.group(1)), int(m.group(2)))] = f
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--base", nargs="+", required=True)
    ap.add_argument("--head", nargs="+", required=True)
    ap.add_argument("--neighbours", nargs="*", default=[])
    ap.add_argument("--auto-upto", type=int, default=120)
    ap.add_argument("--tol", type=int, default=8)
    ap.add_argument("--max-diff", type=float, default=0.1)
    ap.add_argument("--sheet")
    ap.add_argument("--json")
    ap.add_argument("--approved", help="JSON of approved capture pixel hashes: auto pass")
    ap.add_argument("--add-approved", help="add this run's manual captures to that JSON (after the maintainer's OK)")
    ap.add_argument("--note", default="", help="with --add-approved: which series, approved when")
    args = ap.parse_args(argv)
    approved = {}
    if args.approved and os.path.exists(args.approved):
        approved = json.load(open(args.approved, encoding="utf-8")).get("captures", {})

    cache = {}

    def img(path):
        if path not in cache:
            cache[path] = fc.read_png(path)
        return cache[path]

    def same(a, b):
        return fc.compare(img(a), img(b), [], args.tol)[0] <= args.max_diff

    def variants(paths):
        groups = []                      # [representative path, [paths]]
        for p in paths:
            for g in groups:
                if same(g[0], p):
                    g[1].append(p)
                    break
            else:
                groups.append([p, [p]])
        return groups

    ref_runs, base = {}, {}              # (movie, frame) -> [paths]: variants of frame k / all base captures
    for run in args.base + args.neighbours:
        for key, path in captures(run).items():
            base.setdefault(key, []).append(path)
            ref_runs.setdefault(key, []).append(path)   # every clean base run, timed or not
    head = {}
    for run in args.head:
        for key, path in captures(run).items():
            head.setdefault(key, []).append(path)

    def identical(a, b):
        return img(a) == img(b)          # same size, channels and every byte

    def pixel_hash(path):                # size, channels and every byte, as `identical` compares them
        w, h, ch, data = img(path)
        return hashlib.sha256(f"{w}x{h}x{ch}:".encode() + bytes(data)).hexdigest()

    ok, rows, manual = True, [], []      # manual: (key, [head paths that are not identical to a base variant])
    for key in sorted(head):
        movie, frame = key
        name = f"shot_mv{movie:02d}_f{frame:04d}"
        if frame > args.auto_upto:
            left = []
            for h in head[key]:
                run = os.path.basename(os.path.dirname(h))
                if any(identical(r, h) for r in ref_runs.get(key, [])):
                    rows.append({"capture": name, "head_run": run, "verdict": "identical"})
                    print(f"{name}  {run:12s}  identical to a base variant (above {args.auto_upto}: auto pass)")
                elif pixel_hash(h) in approved.get(name, []):
                    rows.append({"capture": name, "head_run": run, "verdict": "approved"})
                    print(f"{name}  {run:12s}  identical to an approved capture (auto pass)")
                else:
                    left.append(h)
            if left:
                manual.append((key, left))
            continue
        ref = ref_runs.get(key, [])
        if not ref:
            ok = False
            rows.append({"capture": name, "verdict": "NO BASE REFERENCE"})
            print(f"{name}  NO BASE REFERENCE")
            continue
        groups = variants(ref)
        near = base.get((movie, frame - 1), []) + base.get((movie, frame + 1), [])
        for h in head[key]:
            run = os.path.basename(os.path.dirname(h))
            if any(same(g[0], h) for g in groups):
                verdict = "match"
            elif any(same(n, h) for n in near):
                verdict = "match +/-1 frame"
            else:
                verdict = "MISMATCH"
                ok = False
            rows.append({"capture": name, "head_run": run, "base_variants": len(groups), "verdict": verdict})
            print(f"{name}  {run:12s}  {len(groups)} base variant(s)  {verdict}")
    n_manual = sum(len(m[1]) for m in manual)
    print(f"{'PASS' if ok else 'FAIL'}: automatic up to frame {args.auto_upto}, identical captures above it; "
          f"{n_manual} capture(s) for the manual visual check")

    if args.sheet:
        parts = ["<!doctype html><meta charset=utf-8><title>G3 rule 4 manual check</title>",
                 "<style>body{font:14px sans-serif;background:#222;color:#eee}img{width:320px;margin:2px}"
                 "h2{margin:18px 0 4px}.b{border:3px solid #4a8}.h{border:3px solid #c84}</style>",
                 "<h1>G3 rule 4 - manual visual check</h1><p>green = base variants (clean base runs), "
                 "orange = head captures. Same picture up to one movie frame of motion = pass.</p>"]
        for key, left in manual:
            movie, frame = key
            parts.append(f"<h2>movie {movie}, decoded frame {frame}</h2><div>")
            for g in variants(ref_runs.get(key, [])):
                runs = ", ".join(os.path.basename(os.path.dirname(p)) for p in g[1])
                parts.append(f"<img class=b src='file:///{html.escape(g[0].replace(os.sep, '/'))}' title='{html.escape(runs)}'>")
            parts.append("</div><div>")
            for h in left:
                run = os.path.basename(os.path.dirname(h))
                parts.append(f"<img class=h src='file:///{html.escape(h.replace(os.sep, '/'))}' title='{html.escape(run)}'>")
            parts.append("</div>")
        with open(args.sheet, "w", encoding="utf-8") as f:
            f.write("\n".join(parts))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"verdict": "pass" if ok else "fail", "auto_upto": args.auto_upto, "rows": rows,
                       "manual": [f"shot_mv{m:02d}_f{k:04d} {os.path.basename(os.path.dirname(h))}"
                                  for (m, k), left in manual for h in left]}, f, indent=1)
    if args.add_approved:
        book = {"note": "pixel hashes (size, channels, bytes) of rule-4 captures the maintainer approved",
                "captures": {}, "approvals": []}
        if os.path.exists(args.add_approved):
            book = json.load(open(args.add_approved, encoding="utf-8"))
        added = 0
        for (m, k), left in manual:
            hashes = book["captures"].setdefault(f"shot_mv{m:02d}_f{k:04d}", [])
            for h in left:
                if pixel_hash(h) not in hashes:
                    hashes.append(pixel_hash(h))
                    added += 1
        book["approvals"].append({"note": args.note, "heads": [os.path.basename(r) for r in args.head],
                                  "added": added})
        with open(args.add_approved, "w", encoding="utf-8") as f:
            json.dump(book, f, indent=1)
        print(f"{added} approved capture hash(es) added to {args.add_approved}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
