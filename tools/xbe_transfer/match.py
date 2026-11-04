"""Map the functions and data of a source build onto a target build.

Passes, repeated until nothing new is found:

1. exact      the function's masked signature occurs exactly once in the target section of the same name
              (all executable sections if the target has no such section).
2. ref        an already matched function names it (call target or address operand) and every matched caller
              agrees where it went; "ref" if its bytes still match there, "ref-changed" if they do not (the body
              changed between the builds - the address is still known, the code needs a look).
3. neighbor   the nearest matched functions before and after it moved by the same amount, and its signature
              matches at its own address moved by that amount.
4. slots      its signature matches at several places, and exactly one of them names, in every slot whose
              address is already known, the address it went to (Burnout 3: ~7,000 initialisers of static floats,
              `movss xmm0,[const]; movss [global],xmm0; ret`, identical once masked and called by no code).

Data addresses come from the same slots: every address operand of a function matched by bytes votes for where
that address went. Addresses no operand names are carried by their section neighbours (`Map.translate`).

Nothing is guessed: a function that none of the passes places is listed as unmatched, with the candidates found.
"""
import bisect
import collections
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Tuple

from .image import Image
from .mask import Signature, signature, slot_values

BYTE_METHODS = ("exact", "ref", "neighbor", "slots")   # placed and verified by bytes: their slots vote


@dataclass
class Func:
    start: int
    size: int
    section: str = ""
    name: str = ""


@dataclass
class FuncMatch:
    dst: int
    method: str
    note: str = ""


@dataclass
class DataMatch:
    dst: int
    votes: int
    others: Dict[int, int] = field(default_factory=dict)   # conflicting targets -> votes


@dataclass
class Map:
    functions: Dict[int, FuncMatch]
    data: Dict[int, DataMatch]
    unmatched: Dict[int, str]                    # source start -> why
    src_funcs: Dict[int, Func]
    src_sections: List[Tuple[str, int, int]]     # name, va, end

    def translate(self, va: int) -> Optional[Tuple[int, str]]:
        """Target address for a source address: function start, inside a function matched by bytes, voted data
        address, or between two voted addresses of the same section that moved by the same amount."""
        m = self.functions.get(va)
        if m:
            return m.dst, "function/" + m.method
        f = self._func_containing(va)
        if f is not None:
            m = self.functions.get(f.start)
            if m and m.method in BYTE_METHODS:
                return m.dst + (va - f.start), "inside/" + m.method
            return None
        d = self.data.get(va)
        if d:
            return d.dst, "data"
        return self._interpolate(va)

    def _func_containing(self, va: int) -> Optional[Func]:
        if not hasattr(self, "_starts"):
            self._starts = sorted(self.src_funcs)
        i = bisect.bisect_right(self._starts, va) - 1
        if i >= 0:
            f = self.src_funcs[self._starts[i]]
            if f.start <= va < f.start + f.size:
                return f
        return None

    def _interpolate(self, va: int) -> Optional[Tuple[int, str]]:
        sec = next(((n, lo, hi) for n, lo, hi in self.src_sections if lo <= va < hi), None)
        if sec is None:
            return None
        known = sorted(a for a in self.data if sec[1] <= a < sec[2])
        i = bisect.bisect_left(known, va)
        if 0 < i < len(known):
            lo, hi = known[i - 1], known[i]
            d1, d2 = self.data[lo].dst - lo, self.data[hi].dst - hi
            if d1 == d2:
                return va + d1, "data-between"
        return None


def build_map(src: Image, funcs: Iterable[Func], dst: Image, min_size: int = 6) -> Map:
    funcs = {f.start: f for f in funcs}
    sigs: Dict[int, Optional[Signature]] = {a: signature(src, a, f.size) for a, f in funcs.items()}
    fmap: Dict[int, FuncMatch] = {}
    candidates: Dict[int, List[int]] = {}

    # 1. exact
    for a in sorted(funcs):
        sig = sigs[a]
        if sig is None or len(sig) < min_size:
            continue
        c = _find(sig, dst, funcs[a].section or (src.section_at(a).name if src.section_at(a) else ""))
        candidates[a] = c
        if len(c) == 1:
            fmap[a] = FuncMatch(c[0], "exact")

    while True:
        new = 0
        # 2. ref
        code_votes, _ = _votes(src, dst, sigs, fmap, funcs)
        for a in sorted(funcs):
            if a in fmap or a not in code_votes:
                continue
            targets = code_votes[a]
            if len(targets) != 1:
                continue
            t = next(iter(targets))
            sig = sigs[a]
            if sig is not None and sig.matches(dst.read(t, len(sig))):
                fmap[a] = FuncMatch(t, "ref")
            else:
                fmap[a] = FuncMatch(t, "ref-changed", "bytes differ at the referenced address")
            new += 1
        # 3. neighbor
        placed = sorted(x for x, m in fmap.items() if m.method in BYTE_METHODS)
        for a in sorted(funcs):
            if a in fmap or sigs[a] is None:
                continue
            i = bisect.bisect_left(placed, a)
            if i == 0 or i == len(placed):
                continue
            lo, hi = placed[i - 1], placed[i]
            if funcs[lo].section != funcs[a].section or funcs[hi].section != funcs[a].section:
                continue
            d = fmap[lo].dst - lo
            if fmap[hi].dst - hi != d:
                continue
            if sigs[a].matches(dst.read(a + d, len(sigs[a]))):
                fmap[a] = FuncMatch(a + d, "neighbor")
                new += 1
        # 4. slots
        if not new:
            new = _slots_pass(src, dst, sigs, fmap, funcs, candidates)
        if not new:
            break

    _, data_votes = _votes(src, dst, sigs, fmap, funcs)
    data = {}
    for a, c in data_votes.items():
        (t, n), *rest = c.most_common()
        data[a] = DataMatch(t, n, dict(rest))

    unmatched = {}
    for a in sorted(funcs):
        if a in fmap:
            continue
        sig = sigs[a]
        if sig is None:
            unmatched[a] = "outside the source image"
        elif len(sig) < min_size:
            unmatched[a] = f"too short to match alone ({len(sig)} bytes) and no matched code refers to it"
        elif candidates.get(a):
            unmatched[a] = "ambiguous: " + ", ".join(f"{c:#010x}" for c in candidates[a][:8])
        else:
            unmatched[a] = "not found (changed or removed)"
    return Map(fmap, data, unmatched, funcs, [(s.name, s.va, s.end) for s in src.sections])


def _slots_pass(src: Image, dst: Image, sigs, fmap, funcs, candidates) -> int:
    """Pass 4: an ambiguous function goes to the one candidate whose slots name the known new addresses."""
    code_votes, data_votes = _votes(src, dst, sigs, fmap, funcs)
    known_data = {a: c.most_common(1)[0][0] for a, c in data_votes.items() if len(c) == 1}
    taken = {m.dst for m in fmap.values()}

    def expected(slot):
        if slot.value in fmap:
            return fmap[slot.value].dst
        if slot.kind == "rel":
            t = code_votes.get(slot.value)
            return next(iter(t)) if t and len(t) == 1 else None
        return known_data.get(slot.value)

    # The candidates of one masked signature are shared by every copy of it: read their slots once per list.
    seen: Dict[tuple, Dict[Tuple[int, ...], List[int]]] = {}
    picks: Dict[int, List[int]] = collections.defaultdict(list)     # target -> sources that chose it
    for a in sorted(funcs):
        c = candidates.get(a)
        if a in fmap or not c or len(c) < 2 or not sigs[a].slots:
            continue
        want = tuple(expected(s) for s in sigs[a].slots)
        if all(w is None for w in want):
            continue
        key = (tuple(c), tuple((s.offset, s.kind) for s in sigs[a].slots))
        if key not in seen:
            by: Dict[Tuple[int, ...], List[int]] = collections.defaultdict(list)
            for t in c:
                if t not in taken:
                    by[tuple(v for _, v in slot_values(sigs[a], dst, t))].append(t)
            seen[key] = by
        hits = [t for vals, ts in seen[key].items()
                if all(w is None or w == v for w, v in zip(want, vals)) for t in ts]
        if len(hits) == 1:
            picks[hits[0]].append(a)
    new = 0
    for t, srcs in picks.items():
        if len(srcs) == 1:                    # two sources on one target: neither is sure
            fmap[srcs[0]] = FuncMatch(t, "slots")
            new += 1
    return new


def _find(sig: Signature, dst: Image, section: str) -> List[int]:
    secs = [s for s in dst.sections if s.name == section] or [s for s in dst.sections if s.executable]
    aoff, key = sig.anchor()
    out = []
    for s in secs:
        if not key:
            break
        i = s.data.find(key)
        while i >= 0:
            start = s.va + i - aoff
            if start >= s.va and sig.matches(dst.read(start, len(sig))):
                out.append(start)
            i = s.data.find(key, i + 1)
    return sorted(set(out))


def _votes(src: Image, dst: Image, sigs, fmap, funcs):
    """Where each address named by byte-matched code went: code (function starts) and data."""
    code = collections.defaultdict(set)
    data = collections.defaultdict(collections.Counter)
    for a, m in fmap.items():
        if m.method not in BYTE_METHODS:
            continue
        for slot, t in slot_values(sigs[a], dst, m.dst):
            if slot.value in funcs:
                code[slot.value].add(t)
            elif slot.kind == "abs":
                data[slot.value][t] += 1
    return code, data
