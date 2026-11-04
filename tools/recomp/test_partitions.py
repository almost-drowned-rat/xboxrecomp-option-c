"""B1 item 2: stable partitions of the generated C (tools/recomp/partitions.py, translate_batch_split
with partitions_path).

Layout: balanced by weight, a function goes to the partition whose range holds its entry address,
aliases by their own entry address; an added function changes only its own partition; only a
partition beyond twice the target weight is split, the others keep their ranges and files.
Codegen: one .c + one declaration header per partition, a check TU that compiles every partition
header with the full header (a conflicting prototype does not compile), unchanged files are not
rewritten, the partition file is the game's (created on the first run, kept after).
"""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

from . import partitions as P
from .lifter import Lifter
from .translator import BatchTranslator

ROOT = Path(__file__).resolve().parents[2]


# -- layout -----------------------------------------------------------------------------------------

def _entries(n, weight=10, step=0x100, base=0x10000):
    return [(base + k * step, weight) for k in range(n)]


def test_plan_balances_by_weight_and_partition_zero_starts_at_zero():
    entries = _entries(8)
    parts = P.plan(entries, 4)
    assert parts[0] == (0, 0) and [i for _, i in parts] == [0, 1, 2, 3]
    members = P.assign(entries, parts)
    assert [len(members[i]) for i in range(4)] == [2, 2, 2, 2]
    heavy = [(0x10000, 70), (0x10100, 10), (0x10200, 10), (0x10300, 10)]
    members = P.assign(heavy, P.plan(heavy, 2))
    assert members[0] == [0x10000] and members[1] == [0x10100, 0x10200, 0x10300]


def test_an_added_function_changes_only_its_own_partition():
    entries = _entries(12)
    parts = P.plan(entries, 3)
    before = P.assign(entries, parts)
    added = sorted(entries + [(0x10000 + 5 * 0x100 + 0x10, 10)])     # between two functions of partition 1
    parts2, _, splits, created = P.layout(added, {"parts": parts, "target": 40.0}, 3)
    after = P.assign(added, parts2)
    assert parts2 == parts and not splits and not created
    changed = [i for i in before if before[i] != after[i]]
    assert changed == [1]


def test_alias_goes_by_its_own_entry_address_even_across_a_partition_boundary():
    host, alias = 0x10000, 0x10380          # alias entry inside the host's code
    entries = [(host, 50), (0x10200, 10), (alias, 10), (0x10400, 10)]
    parts = [(0, 0), (0x10300, 1)]           # boundary between host and alias
    members = P.assign(entries, parts)
    assert host in members[0] and alias in members[1]
    assert members[1] == [alias, 0x10400]
    # the same layout from the partition file (load sorts it) assigns the same way
    assert P.assign(entries, P.load_parts(P.dumps(list(reversed(parts)), 1.0))) == members


def test_only_an_oversized_partition_is_split_and_the_others_keep_their_index():
    entries = _entries(9)
    parts = P.plan(entries, 3)               # 3 x 3 functions, target 30
    grown = entries + [(0x10000 + 4 * 0x100 + k, 10) for k in range(1, 6)]   # partition 1 to 8 functions
    grown.sort()
    parts2, target, splits, _ = P.layout(grown, {"parts": parts, "target": 30.0}, 3)
    assert target == 30.0 and [s[0] for s in splits] == [1] and splits[0][1] == 3
    old = P.assign(entries, parts)
    new = P.assign(grown, parts2)
    assert new[0] == old[0] and new[2] == old[2]
    assert sorted(new[1] + new[3]) == sorted(a for a, _ in grown if 0x10300 <= a < 0x10600)
    assert all(sum(10 for _ in new[i]) <= 2 * target for i in new)


def test_partition_file_round_trip_and_validation(tmp_path):
    parts = [(0, 0), (0x10300, 2), (0x10600, 1)]
    f = tmp_path / "partitions.json"
    f.write_text(P.dumps(parts, 1234.4), encoding="utf-8")
    loaded = P.load(str(f))
    assert loaded == {"parts": sorted(parts), "target": 1234.0}
    assert P.dumps(loaded["parts"], loaded["target"]) == f.read_text(encoding="utf-8")
    assert P.load(str(tmp_path / "none.json")) is None
    f.write_text(P.dumps([(0x100, 0)], 1), encoding="utf-8")
    with pytest.raises(ValueError):
        P.load(str(f))


# -- codegen ----------------------------------------------------------------------------------------

class _FakeTranslator:
    """Every function calls the next one by name, like RECOMP_ABI_CALL does."""

    def __init__(self, addrs):
        self.owned_function_starts = set()
        self.lifter = Lifter()
        self.addrs = sorted(addrs)

    def translate_function(self, addr, func_info):
        later = [a for a in self.addrs if a > addr]
        call = f"sub_{later[0]:08X}(ctx, base); " if later else ""
        return (f"/* sub_{addr:08X} */\nvoid sub_{addr:08X}(RECOMP_PARAMS)\n"
                f"{{ (void)ctx; (void)base; {call}}}\n")


def _run(addrs, gen, pfile, **kw):
    batch = BatchTranslator.__new__(BatchTranslator)
    batch.translator = _FakeTranslator(addrs)
    funcs = [(a, {"name": f"sub_{a:08X}"}) for a in sorted(addrs)]
    return batch.translate_batch_split(funcs, str(gen), chunk_size=4, partitions_path=str(pfile), **kw)


def _mtimes(gen):
    return {p.name: p.stat().st_mtime_ns for p in Path(gen).iterdir()}


def test_codegen_writes_partitions_with_own_headers_and_rewrites_only_what_changed(tmp_path):
    gen, pfile = tmp_path / "gen", tmp_path / "game" / "partitions.json"
    pfile.parent.mkdir()
    addrs = [0x10000 + k * 0x100 for k in range(12)]
    st = _run(addrs, gen, pfile)
    assert st["partitions_created"] and st["num_chunks"] == 3 and pfile.exists()
    names = sorted(p.name for p in gen.iterdir())
    for i in range(3):
        assert f"recomp_{i:04d}.c" in names and f"recomp_{i:04d}.h" in names
    c1 = (gen / "recomp_0001.c").read_text(encoding="utf-8")
    h1 = (gen / "recomp_0001.h").read_text(encoding="utf-8")
    assert '#include "recomp_0001.h"' in c1 and "recomp_funcs.h" not in c1
    # partition 1 holds 0x10400..0x10700 and calls 0x10800 (partition 2): declared, nothing else far away
    assert "void sub_00010800(RECOMP_PARAMS);" in h1 and "sub_00010000" not in h1

    st = _run(addrs, gen, pfile)                         # same input: nothing rewritten
    assert st["partitions_written"] == [] and not st["partitions_created"]

    before = _mtimes(gen)
    st = _run(addrs + [0x10450], gen, pfile)             # one function more, inside partition 1
    rewritten = set(st["partitions_written"])
    assert {"recomp_0001.c", "recomp_0001.h"} <= rewritten
    assert not rewritten & {"recomp_0000.c", "recomp_0000.h", "recomp_0002.c", "recomp_0002.h"}
    after = _mtimes(gen)
    for f in ("recomp_0000.c", "recomp_0000.h", "recomp_0002.c", "recomp_0002.h"):
        assert before[f] == after[f], f


def test_repartition_rebalances_and_removes_stale_partition_files(tmp_path):
    gen, pfile = tmp_path / "gen", tmp_path / "partitions.json"
    addrs = [0x10000 + k * 0x100 for k in range(12)]
    _run(addrs, gen, pfile)
    assert (gen / "recomp_0002.c").exists()
    st = _run(addrs[:8], gen, pfile, repartition=True)  # 8 functions, 4 per partition -> 2 partitions
    assert st["partitions_created"] and st["num_chunks"] == 2
    assert not (gen / "recomp_0002.c").exists() and not (gen / "recomp_0002.h").exists()


def _cc():
    return shutil.which("clang") or shutil.which("gcc")


def _compile(cc, src, gen):
    return subprocess.run([cc, "-fsyntax-only", "-w", "-I", str(gen), "-I", str(ROOT / "templates" / "runtime"),
                           str(src)], capture_output=True, text=True)


def test_partitions_and_the_declaration_check_compile_and_a_conflicting_prototype_does_not(tmp_path):
    cc = _cc()
    if not cc:
        pytest.skip("no C compiler on PATH")
    gen, pfile = tmp_path / "gen", tmp_path / "partitions.json"
    _run([0x10000 + k * 0x100 for k in range(12)], gen, pfile)
    for src in sorted(gen.glob("recomp_*.c")):
        r = _compile(cc, src, gen)
        assert r.returncode == 0, (src.name, r.stderr[-2000:])
    check = (gen / "recomp_declcheck.c").read_text(encoding="utf-8")
    assert all(f'#include "recomp_{i:04d}.h"' in check for i in range(3))
    assert "RECOMP_DECLCHECK(sub_00010800);" in check
    # a partition header that disagrees with the generated type: a compile error in the check TU
    h = gen / "recomp_0001.h"
    h.write_text(h.read_text(encoding="utf-8").replace(
        "void sub_00010800(RECOMP_PARAMS);", "void sub_00010800(int);"), encoding="utf-8")
    r = _compile(cc, gen / "recomp_declcheck.c", gen)
    assert r.returncode != 0 and "sub_00010800" in r.stderr
