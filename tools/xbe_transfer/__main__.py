"""py -3 -m tools.xbe_transfer {match,remap} ...  (see the package docstring)"""
import argparse
import collections
import json
import os
import sys

from .image import from_xbe
from .match import DataMatch, Func, FuncMatch, Map, build_map
from .remap import remap_file


def h(v: int) -> str:
    return f"0x{v:08X}"


def save(m: Map, path: str, meta: dict) -> None:
    obj = {
        "meta": meta,
        "src_sections": [[n, h(lo), h(hi)] for n, lo, hi in m.src_sections],
        "src_functions": [[h(f.start), f.size, f.section, f.name] for f in sorted(m.src_funcs.values(), key=lambda f: f.start)],
        "functions": {h(a): {"dst": h(x.dst), "method": x.method, **({"note": x.note} if x.note else {})}
                      for a, x in sorted(m.functions.items())},
        "data": {h(a): {"dst": h(x.dst), "votes": x.votes, **({"others": {h(k): v for k, v in x.others.items()}} if x.others else {})}
                 for a, x in sorted(m.data.items())},
        "unmatched": {h(a): why for a, why in sorted(m.unmatched.items())},
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=1)


def load(path: str) -> Map:
    o = json.load(open(path, encoding="utf-8"))
    i = lambda s: int(s, 16)
    return Map(
        functions={i(a): FuncMatch(i(x["dst"]), x["method"], x.get("note", "")) for a, x in o["functions"].items()},
        data={i(a): DataMatch(i(x["dst"]), x["votes"], {i(k): v for k, v in x.get("others", {}).items()}) for a, x in o["data"].items()},
        unmatched={i(a): why for a, why in o["unmatched"].items()},
        src_funcs={i(s): Func(i(s), size, sec, name) for s, size, sec, name in o["src_functions"]},
        src_sections=[(n, i(lo), i(hi)) for n, lo, hi in o["src_sections"]],
    )


def summary(m: Map) -> str:
    c = collections.Counter(x.method for x in m.functions.values())
    n = len(m.src_funcs)
    lines = [f"functions: {n}, matched {len(m.functions)} ({100 * len(m.functions) / max(n, 1):.1f} %), unmatched {len(m.unmatched)}"]
    lines += [f"  {k:12} {v}" for k, v in sorted(c.items())]
    conflicts = sum(1 for d in m.data.values() if d.others)
    lines.append(f"data addresses: {len(m.data)} (with conflicting votes: {conflicts})")
    deltas = collections.Counter()
    for a, x in m.functions.items():
        deltas[(m.src_funcs[a].section, x.dst - a)] += 1
    lines.append("most common moves (section, delta, functions):")
    lines += [f"  {s:10} {d:+#x} {k}" for (s, d), k in deltas.most_common(12)]
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="tools.xbe_transfer", description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("match", help="build the source -> target map")
    a.add_argument("--src-xbe", required=True)
    a.add_argument("--src-functions", required=True, help="functions.json of the source build (tools.disasm)")
    a.add_argument("--dst-xbe", required=True)
    a.add_argument("-o", "--out", required=True, help="map.json")
    a.add_argument("--report", help="text report (summary + unmatched)")
    a.add_argument("--min-size", type=int, default=6)
    b = sub.add_parser("remap", help="rewrite files with a map")
    b.add_argument("--map", required=True)
    b.add_argument("--out-dir", required=True)
    b.add_argument("--interpolate", action="store_true", help="also rewrite data addresses between two known ones")
    b.add_argument("files", nargs="+")
    args = ap.parse_args(argv)

    if args.cmd == "match":
        src, dst = from_xbe(args.src_xbe), from_xbe(args.dst_xbe)
        funcs = []
        for f in json.load(open(args.src_functions, encoding="utf-8")):
            start = int(f["start"], 16) if isinstance(f["start"], str) else f["start"]
            funcs.append(Func(start, int(f["size"]), f.get("section", ""), f.get("name", "")))
        m = build_map(src, funcs, dst, args.min_size)
        save(m, args.out, {"src_xbe": os.path.basename(args.src_xbe), "dst_xbe": os.path.basename(args.dst_xbe)})
        text = summary(m)
        print(text)
        if args.report:
            with open(args.report, "w", encoding="utf-8") as f:
                f.write(text + "\n\nunmatched:\n")
                for s, why in sorted(m.unmatched.items()):
                    fn = m.src_funcs[s]
                    f.write(f"  {h(s)} {fn.size:6} {fn.section:8} {fn.name}: {why}\n")
                f.write("\nchanged bodies (address known from references, bytes differ):\n")
                for s, x in sorted(m.functions.items()):
                    if x.method == "ref-changed":
                        f.write(f"  {h(s)} -> {h(x.dst)} {m.src_funcs[s].name}\n")
        return 0

    m = load(args.map)
    os.makedirs(args.out_dir, exist_ok=True)
    left = 0
    with open(os.path.join(args.out_dir, "remap_report.txt"), "w", encoding="utf-8") as rep:
        for p in args.files:
            r = remap_file(m, p, args.interpolate)
            with open(os.path.join(args.out_dir, os.path.basename(p)), "w", encoding="utf-8", newline="") as f:
                f.write(r.text)
            for c in r.changes:
                rep.write(f"{c.where}: {h(c.old)} -> {h(c.new) if c.new is not None else '-':10} {c.how}\n")
                left += c.new is None
    print(f"written to {args.out_dir}; {left} address(es) left alone - see remap_report.txt")
    return 1 if left else 0


if __name__ == "__main__":
    sys.exit(main())
