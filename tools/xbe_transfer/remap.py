"""Rewrite the addresses in seeds, overrides and hook files with a Map.

Text files (C, TOML, ...): `sub_XXXXXXXX` names, also with a suffix (`sub_XXXXXXXX_gen`, the generated body a
wrapper calls), and hex literals of 6-8 digits, also with a C integer suffix (`0x0035FB48u`). A literal is only rewritten when the
map knows it as code (a function start, or inside a function matched by bytes) or as a data address some matched
code names; a number that merely falls in the image range (a size, a mask) is left alone. Addresses between two known
data addresses are only rewritten with `interpolate=True`. JSON files: every string or integer value that is such an
address. Every change and every in-range literal that was left alone goes into the report, so the result can be
reviewed line by line.
"""
import json
import re
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

from .match import Map

_HEX = re.compile(r"(?<![0-9A-Za-z_])(?:(sub_)([0-9A-Fa-f]{6,8})(?=(?:_[A-Za-z]\w*)?(?!\w))"
                  r"|(0x)([0-9A-Fa-f]{6,8})(?=(?:[uUlL]{1,3})?(?!\w)))")


@dataclass
class Change:
    where: str
    old: int
    new: Optional[int]
    how: str


@dataclass
class Result:
    text: str
    changes: List[Change] = field(default_factory=list)


def _lookup(m: Map, va: int, interpolate: bool) -> Optional[Tuple[int, str]]:
    r = m.translate(va)
    if r is None:
        return None
    if r[1] == "data-between" and not interpolate:
        return None
    return r


def _in_range(m: Map, va: int) -> bool:
    return any(lo <= va < hi for _, lo, hi in m.src_sections)


def remap_text(m: Map, text: str, name: str = "", interpolate: bool = False) -> Result:
    res = Result("")
    out = []
    for ln, line in enumerate(text.splitlines(keepends=True), 1):
        def sub(mo):
            prefix, digits = (mo.group(1), mo.group(2)) if mo.group(1) else (mo.group(3), mo.group(4))
            va = int(digits, 16)
            if not _in_range(m, va):
                return mo.group(0)
            r = _lookup(m, va, interpolate)
            if r is None:
                res.changes.append(Change(f"{name}:{ln}", va, None, "left alone (not known as code or data)"))
                return mo.group(0)
            new, how = r
            res.changes.append(Change(f"{name}:{ln}", va, new, how))
            nd = f"{new:0{len(digits)}X}" if digits.upper() == digits else f"{new:0{len(digits)}x}"
            return prefix + nd          # the suffix (`_gen`, `u`) is outside the match and stays
        out.append(_HEX.sub(sub, line))
    res.text = "".join(out)
    return res


def remap_json(m: Map, obj, name: str = "", interpolate: bool = False):
    changes: List[Change] = []

    def walk(o, path):
        if isinstance(o, dict):
            return {k: walk(v, f"{path}.{k}") for k, v in o.items()}
        if isinstance(o, list):
            return [walk(v, f"{path}[{i}]") for i, v in enumerate(o)]
        if isinstance(o, str) and re.fullmatch(r"0x[0-9A-Fa-f]{6,8}", o):
            r = remap_text(m, o, f"{name}{path}", interpolate)
            changes.extend(r.changes)
            return r.text
        if isinstance(o, int) and not isinstance(o, bool) and _in_range(m, o):
            r = _lookup(m, o, interpolate)
            changes.append(Change(f"{name}{path}", o, r[0] if r else None, r[1] if r else "left alone (not known as code or data)"))
            return r[0] if r else o
        return o

    return walk(obj, ""), changes


def remap_file(m: Map, path: str, interpolate: bool = False) -> Result:
    text = open(path, encoding="utf-8").read()
    name = path.replace("\\", "/").rsplit("/", 1)[-1]
    if path.lower().endswith(".json"):
        obj, changes = remap_json(m, json.loads(text), name, interpolate)
        return Result(json.dumps(obj, indent=1) + "\n", changes)
    return remap_text(m, text, name, interpolate)
