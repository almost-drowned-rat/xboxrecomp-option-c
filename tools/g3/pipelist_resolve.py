"""pipelist_resolve.py <list> <game_dir> [-o <out>]

The shipped pipeline list's second half (src/nv2a_vk/nv2a_vk.c "shipped pipeline list"; maintainer 2026-11-15,
step 6: keys only, no game data). A recording (RECOMP_VK_PIPELIST_RECORD=<list>) writes the keys into <list> and,
for every vertex program that is not in the XBE image, its microcode into the developer-only side file
<list>.vp. This tool finds each such program in the title's disc files (<game_dir>, the extracted disc), writes the
file (relative, '/') and the offset into the program's record, removes duplicate records, and writes <out> (default:
<list>.resolved). The side file stays on the developer's machine; only <out> ships. Prints what it found.

Record layout (version 2, little endian): header "XLP1" + u32 2; PL_PROG (256 bytes): kind 1, xform, vp_start,
slots, u64 vkey, vp_va, vp_words, file_off, pad, char file[64], Nv2aPshKey (152); PL_PIPE (224 bytes): kind 2, vi,
PipeRecVi.
"""
import os
import struct
import sys

PROG, PIPE = 256, 224


def records(data):
    off = 8
    while off < len(data):
        kind = struct.unpack_from("<I", data, off)[0]
        size = PROG if kind == 1 else PIPE if kind == 2 else None
        if size is None or off + size > len(data):
            raise SystemExit(f"bad record kind {kind} at {off}")
        yield kind, bytearray(data[off:off + size])
        off += size


def side_programs(path):
    out = {}
    if not os.path.exists(path):
        return out
    d = open(path, "rb").read()
    off = 0
    while off + 16 <= len(d):
        lo, hi, words, _ = struct.unpack_from("<IIII", d, off)
        off += 16
        out[(hi << 32) | lo] = d[off:off + words * 16]
        off += words * 16
    return out


def main():
    args = [a for a in sys.argv[1:]]
    if len(args) < 2:
        print(__doc__)
        return 2
    out = args[args.index("-o") + 1] if "-o" in args else args[0] + ".resolved"
    lst, game = args[0], args[1]
    data = open(lst, "rb").read()
    if data[:4] != b"XLP1" or struct.unpack_from("<I", data, 4)[0] != 2:
        raise SystemExit("not a version-2 pipeline list")
    micro = side_programs(lst + ".vp")
    want = {}
    for kind, r in records(data):
        if kind == 1:
            xform, = struct.unpack_from("<I", r, 4)
            vkey, = struct.unpack_from("<Q", r, 16)
            va, = struct.unpack_from("<I", r, 24)
            if xform == 2 and not va and vkey in micro:
                want[vkey] = micro[vkey]
    found = {}
    for root, _, files in os.walk(game):
        for fn in sorted(files):
            path = os.path.join(root, fn)
            rel = os.path.relpath(path, game).replace(os.sep, "/")
            if len(rel.encode()) >= 64:
                continue
            try:
                blob = open(path, "rb").read()
            except OSError:
                continue
            for vkey, code in want.items():
                if vkey in found:
                    continue
                j = blob.find(code)
                if j >= 0:
                    found[vkey] = (rel, j)
            if len(found) == len(want):
                break
    seen, keep = set(), [data[:8]]
    stats = {"prog_ff": 0, "prog_xbe": 0, "prog_file": 0, "prog_none": 0, "pipes": 0, "dups": 0}
    for kind, r in records(data):
        if kind == 1:
            xform, = struct.unpack_from("<I", r, 4)
            vkey, = struct.unpack_from("<Q", r, 16)
            va, = struct.unpack_from("<I", r, 24)
            if xform == 2 and not va and vkey in found:
                rel, j = found[vkey]
                struct.pack_into("<I", r, 32, j)
                r[40:104] = rel.encode().ljust(64, b"\0")
            key = bytes(r[4:32]) + bytes(r[104:])          # identity without the location
            if key in seen:
                stats["dups"] += 1
                continue
            seen.add(key)
            if xform != 2:
                stats["prog_ff"] += 1
            elif va:
                stats["prog_xbe"] += 1
            elif r[40]:
                stats["prog_file"] += 1
            else:
                stats["prog_none"] += 1
        else:
            key = bytes(r)
            if key in seen:
                stats["dups"] += 1
                continue
            seen.add(key)
            stats["pipes"] += 1
        keep.append(bytes(r))
    open(out, "wb").write(b"".join(keep))
    print(f"{out}: programs fixed-function {stats['prog_ff']}, vertex in XBE {stats['prog_xbe']}, vertex in disc files "
          f"{stats['prog_file']}, vertex without location {stats['prog_none']}; pipelines {stats['pipes']}; "
          f"{stats['dups']} duplicates dropped; files: {sorted(set(f for f, _ in found.values()))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
