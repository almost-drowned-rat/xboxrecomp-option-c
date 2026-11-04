#!/usr/bin/env python3
"""relift_compare.py - the relift gate of a lifter change: nothing the base lift had may disappear.

    py -3 -I tools/relift_compare.py <base dir> <head dir> [--report out.txt] [--allow-lost <file>]

A dir is a lift's private dir with pipeline/disasm/functions.json and gen/recomp_stubs_unresolved.c (the bench's
build.ps1 layout). Compared:

  functions   every function start of the base must be in the head (detected by any method); a lost start fails.
  aliases     every tail-jump alias of the base (detection_method tail_jump_alias) must be a function of the head;
              a lost one fails - the v0.14 reachability filter dropped 934 of them (docs/lessons_learned.md).
  stubs       unresolved call stubs, compared BY ADDRESS (not by count): a stub the head has and the base did not
              fails - a call that went to a function now goes to a no-op. A stub that disappears is a gain.

Added functions and removed stubs are listed, not judged. `--allow-lost` names addresses (one hex per line, # comments)
a change is expected to drop, each with its reason in the comment; anything else lost fails. Exit 0 = pass, 1 = fail,
2 = usage. Generic: no title-specific addresses.
"""
import argparse
import json
import os
import re
import sys

STUB = re.compile(r"^void sub_([0-9A-Fa-f]{8})\(RECOMP_PARAMS\)")


def load_functions(d):
    with open(os.path.join(d, "pipeline", "disasm", "functions.json"), encoding="utf-8") as f:
        funcs = json.load(f)
    starts, aliases = set(), set()
    for fn in funcs:
        a = int(fn["start"], 16)
        starts.add(a)
        if fn.get("detection_method") == "tail_jump_alias":
            aliases.add(a)
    return starts, aliases


def load_stubs(d):
    path = os.path.join(d, "gen", "recomp_stubs_unresolved.c")
    out = set()
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = STUB.match(line)
            if m:
                out.add(int(m.group(1), 16))
    return out


def load_allowed(path):
    allowed = set()
    if not path:
        return allowed
    with open(path, encoding="utf-8") as f:
        for line in f:
            s = line.split("#", 1)[0].strip()
            if s:
                allowed.add(int(s, 16))
    return allowed


def compare(base, head, allowed=frozenset()):
    """Returns (ok, lines)."""
    bf, ba = load_functions(base)
    hf, ha = load_functions(head)
    bs, hs = load_stubs(base), load_stubs(head)
    lost_f = sorted((bf - hf) - allowed)
    lost_a = sorted((ba - hf) - allowed)
    new_s = sorted(hs - bs)
    lines = [
        f"functions: base {len(bf)}, head {len(hf)}, added {len(hf - bf)}, lost {len(bf - hf)}"
        f" ({len(lost_f)} not allowed)",
        f"aliases:   base {len(ba)}, head {len(ha)}, base aliases not a head function: {len(ba - hf)}"
        f" ({len(lost_a)} not allowed)",
        f"stubs:     base {len(bs)}, head {len(hs)}, new {len(new_s)}, gone {len(bs - hs)}",
    ]
    for name, xs in (("LOST function", lost_f), ("LOST alias", lost_a), ("NEW stub", new_s)):
        lines += [f"  {name} 0x{a:08X}" for a in xs]
    lines += [f"  added function 0x{a:08X}" for a in sorted(hf - bf)]
    lines += [f"  stub gone 0x{a:08X}" for a in sorted(bs - hs)]
    ok = not lost_f and not lost_a and not new_s
    lines.insert(0, "PASS" if ok else "FAIL")
    return ok, lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base")
    ap.add_argument("head")
    ap.add_argument("--report")
    ap.add_argument("--allow-lost")
    a = ap.parse_args()
    for d in (a.base, a.head):
        if not os.path.exists(os.path.join(d, "pipeline", "disasm", "functions.json")):
            print(f"no pipeline/disasm/functions.json under {d}", file=sys.stderr)
            return 2
    ok, lines = compare(a.base, a.head, load_allowed(a.allow_lost))
    if a.report:
        with open(a.report, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    print("\n".join(lines[:4]))
    for line in lines[4:]:
        if line.startswith(("  LOST", "  NEW")):
            print(line)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
