"""Cross-profile calibration of a G3 reference: how one display profile's numbers map onto the other's.

Measurement rule (maintainer, 2026-10-30; toolkit-wide, every port calibrates its own - offsets are per port): the
machine has exactly two measurement display profiles, switched freely between windows. Instead of one reference per
profile, the same build with the same frozen caches is run >= 5 times on each (interleaved where possible), and per
metric this reports:

  mean per profile, offset d = mean(B) - mean(A), its uncertainty u = 2 * sqrt(sA^2/nA + sB^2/nB) (about 95 %),
  the A/A tolerance T = the larger within-profile range (what two runs of one profile differ by anyway), and
  equivalent  |d| <= T: no correction for this metric
  corrected   |d| > T and u <= T: offset d +- u is stored; a G3 against a reference of the other profile applies it
              and widens that metric's tolerance by u (verdict tagged "cross-profile")
  unstable    u > T: the offset is not known well enough - this metric is same-profile only

Milestones (first-open times) are also checked for shape: one additive offset for all of them, or one proportional
factor (d_i = k * t_i); the model with the smaller residual is reported. RAM is expected to have no offset.

Run dirs are ref_run outputs (summary.json, optional menu.json). A repeated run (<name>_try2/3) is the caller's to
pick. Recalibrate after a GPU driver or Windows update or a new reference (g3_series logs both with every run).

  python tools/profile_calibration.py --a PANEL1 <run dirs...> --b PANEL2 <run dirs...> \
      --milestone englis=englis30.xmv --milestone femain=_femain.rws [--json calibration.json]
"""
import argparse
import json
import math
import os
import sys

SCALARS = [("avg_cpu_percent", "CPU %"), ("peak_private_mb", "private MB"), ("peak_working_set_mb", "working set MB")]
MENU = [("menu_response_ms", "menu response ms"), ("frame_ms_p50", "frame p50 ms"), ("frame_ms_p99", "frame p99 ms"),
        ("frames_over_50ms", "frames > 50 ms")]


def load(run):
    with open(os.path.join(run, "summary.json"), encoding="utf-8-sig") as f:
        s = json.load(f)
    m = {}
    mp = os.path.join(run, "menu.json")
    if os.path.exists(mp):
        with open(mp, encoding="utf-8-sig") as f:
            m = json.load(f)
    return s, m


def values(runs, milestones):
    out = {}
    for run in runs:
        s, m = load(run)
        fo = s.get("first_open_ms", {})
        for name, suffix in milestones:
            hits = [v for k, v in fo.items() if k.lower().endswith("\\" + suffix.lower())]
            out.setdefault(("milestone", name), []).append(min(hits) / 1000.0 if hits else None)
        for key, label in SCALARS:
            out.setdefault(("scalar", label), []).append(s.get(key))
        for key, label in MENU:
            out.setdefault(("menu", label), []).append(m.get(key))
    return out


def stats(xs):
    xs = [x for x in xs if x is not None]
    n = len(xs)
    if not n:
        return None
    mean = sum(xs) / n
    sd = math.sqrt(sum((x - mean) ** 2 for x in xs) / (n - 1)) if n > 1 else 0.0
    return {"n": n, "mean": mean, "sd": sd, "min": min(xs), "max": max(xs), "range": max(xs) - min(xs)}


def compare(a, b):
    if not a or not b:
        return None
    d = b["mean"] - a["mean"]
    u = 2.0 * math.sqrt(a["sd"] ** 2 / a["n"] + b["sd"] ** 2 / b["n"])
    t = max(a["range"], b["range"])
    verdict = "equivalent" if abs(d) <= t else ("unstable" if u > t else "corrected")
    return {"offset": d, "uncertainty": u, "aa_tolerance": t, "relative": d / a["mean"] if a["mean"] else None,
            "verdict": verdict}


def shape(rows):
    """Additive (d_i = c) or proportional (d_i = k * t_i) for the milestone offsets."""
    pts = [(r["a"]["mean"], r["cmp"]["offset"]) for r in rows if r["cmp"]]
    if len(pts) < 2:
        return None
    c = sum(d for _, d in pts) / len(pts)
    k = sum(t * d for t, d in pts) / sum(t * t for t, _ in pts)
    res_add = math.sqrt(sum((d - c) ** 2 for _, d in pts) / len(pts))
    res_prop = math.sqrt(sum((d - k * t) ** 2 for t, d in pts) / len(pts))
    return {"additive_offset_s": c, "additive_residual_s": res_add, "proportional_factor": k,
            "proportional_residual_s": res_prop, "better": "additive" if res_add <= res_prop else "proportional"}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--a", nargs="+", required=True, help="profile name, then its run dirs")
    ap.add_argument("--b", nargs="+", required=True, help="profile name, then its run dirs")
    ap.add_argument("--milestone", action="append", default=[], help="name=file suffix of a first open")
    ap.add_argument("--json", help="write the calibration here")
    args = ap.parse_args()
    na, runs_a = args.a[0], args.a[1:]
    nb, runs_b = args.b[0], args.b[1:]
    milestones = [tuple(x.split("=", 1)) for x in args.milestone]
    if len(runs_a) < 5 or len(runs_b) < 5:
        print(f"warning: {len(runs_a)} {na} / {len(runs_b)} {nb} runs - the rule asks for >= 5 per profile",
              file=sys.stderr)
    va, vb = values(runs_a, milestones), values(runs_b, milestones)
    rows = []
    for key in va:
        a, b = stats(va[key]), stats(vb.get(key, []))
        rows.append({"kind": key[0], "metric": key[1], "a": a, "b": b, "cmp": compare(a, b)})

    print(f"profile A = {na} ({len(runs_a)} runs), B = {nb} ({len(runs_b)} runs); offset = B - A")
    print(f"{'metric':18} {'mean A':>10} {'range A':>8} {'mean B':>10} {'range B':>8} {'offset':>9} {'+-u':>7}"
          f" {'A/A tol':>8} {'rel':>7}  verdict")
    for r in rows:
        a, b, c = r["a"], r["b"], r["cmp"]
        if not c:
            print(f"{r['metric']:18} (missing)")
            continue
        rel = f"{100 * c['relative']:6.2f}%" if c["relative"] is not None else "      -"
        print(f"{r['metric']:18} {a['mean']:10.3f} {a['range']:8.3f} {b['mean']:10.3f} {b['range']:8.3f}"
              f" {c['offset']:+9.3f} {c['uncertainty']:7.3f} {c['aa_tolerance']:8.3f} {rel}  {c['verdict']}")
    sh = shape([r for r in rows if r["kind"] == "milestone"])
    if sh:
        print(f"milestone shape: additive {sh['additive_offset_s']:+.3f} s (residual {sh['additive_residual_s']:.3f} s),"
              f" proportional x{1 + sh['proportional_factor']:.5f} (residual {sh['proportional_residual_s']:.3f} s)"
              f" -> {sh['better']}")
    ram = next((r for r in rows if r["metric"] == "private MB" and r["cmp"]), None)
    if ram:
        print(f"RAM (private) offset {ram['cmp']['offset']:+.2f} MB +- {ram['cmp']['uncertainty']:.2f}:"
              f" {'zero within A/A noise, as expected' if ram['cmp']['verdict'] == 'equivalent' else 'NOT zero - check'}")
    equivalent = all(r["cmp"]["verdict"] == "equivalent" for r in rows if r["cmp"])
    print(f"profiles equivalent: {'yes' if equivalent else 'no'}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"profile_a": na, "profile_b": nb, "runs_a": runs_a, "runs_b": runs_b,
                       "offset": "b - a", "equivalent": equivalent, "milestone_shape": sh, "metrics": rows}, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
