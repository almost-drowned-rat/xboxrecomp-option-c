"""Convert a seed_functions.json (tools.disasm --seed-functions) into [[function]] tables for a variant file
of the game config (tools/gameconfig.py).

    py -3 -m tools.seeds_to_toml config/seed_functions.json >> config/variants/us.toml

"observed" follows tools.disasm's reading of the JSON: the field itself, or "observed at runtime" in the note
(seed_from_log wrote that before it set the field), so the converted seeds mark the same entries observed.
"""
import json
import sys


def _toml_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def convert(entries):
    out = []
    for e in entries:
        if isinstance(e, int):
            e = {"start": hex(e)}
        at = int(e["start"], 16)
        lines = ["[[function]]", f"at = 0x{at:08X}"]
        if e.get("note"):
            lines.append(f"note = {_toml_str(e['note'])}")
        if e.get("observed") or "observed at runtime" in e.get("note", ""):
            lines.append("observed = true")
        unknown = set(e) - {"start", "note", "observed"}
        if unknown:
            raise ValueError(f"seed 0x{at:08X}: unknown key(s) {sorted(unknown)}")
        out.append("\n".join(lines))
    return "\n\n".join(out) + "\n"


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[0], encoding="utf-8") as f:
        sys.stdout.write(convert(json.load(f)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
