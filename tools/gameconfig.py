"""Per-game configuration in TOML (B1 item 3): one common file plus one file per disc variant.

    config/game.toml               common part; lists the variants
    config/variants/<id>.toml      one per variant (US, PAL A, PAL B ...): what differs between discs

The variant is chosen by the SHA-256 of the default.xbe being lifted, so the US XBE can never be lifted with
the PAL seeds. Read with the standard tomllib (Python >= 3.11). Paths are relative to the file they appear in.
Unknown keys, an unknown schema_version and an unknown XBE are errors: a typo must not be ignored silently.
The tools take the config with --game-config; a flag given on the command line overrides the config value.

game.toml:

    schema_version = 1
    [game]
    title = "Burnout 3: Takedown"          # informational
    [disasm]                               # common to all variants (each table optional)
    extra_sections = ["XIPS"]              #   --extra-sections
    text_only = true                       #   --text-only: only .text (+ extra_sections) is code - for a title
                                           #   with executable-flagged data or DSP sections (Orta APPDATA, DOLBY)
    [recomp]
    split = 1000                           #   --split
    exclude_manual = "../src/game/recomp/recomp_manual.c"   # --exclude-manual
    weak_functions = false                 #   --weak-functions
    spin_backoff = false                   #   --spin-backoff: polling loops back off (tools/recomp/spin_detect.py)
    [variant.us]                           # one table per variant; the id names the file
    label = "USA"
    sha256 = "..."                         # of default.xbe
    file = "variants/us.toml"

variants/<id>.toml (everything optional):

    schema_version = 1
    partitions = "us.partitions.json"      # the generated partition layout (--partitions); never written here
    [disasm]                               # / [recomp]: the same keys as above, override the common value
    [[function]]                           # seed entry point (--seed-functions)
    at = 0x001D2793
    note = "..."
    observed = true                        # a run reached it (seed_from_log)
    [[hook]]                               # instruction hook, format of tools/recomp/hooks_config.py
    [[switch]]                             # reserved: no switch-table override in the lifter yet -> error if used
    [d3d_block_on_time]                    # XDK D3D's fence wait without the spin (src/xdk/xdk_d3d_wait.h): the
    function = 0x002AFF90                  #   title's BlockOnTime - the lifter emits it as sub_X_gen plus a
    device = 0x002BB788                    #   wrapper sub_X that waits first (gen/recomp_hle.c), unless the
    sem_off = 0x30                         #   port's recomp_manual.c defines sub_X itself (that wins)
    put_off = 0x2C                         #   g_pDevice; the device fields: GPU semaphore pointer, put value

The generator never rewrites these files; the partition layout stays its own generated file that the variant
only references.
"""
import hashlib
import os
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

SCHEMA_VERSIONS = {1}

try:
    import tomllib
except ImportError:          # Python < 3.11
    tomllib = None

_COMMON_KEYS = {"schema_version", "game", "disasm", "recomp", "variant"}
_GAME_KEYS = {"title"}
_DISASM_KEYS = {"extra_sections": list, "text_only": bool}
_RECOMP_KEYS = {"split": int, "exclude_manual": str, "weak_functions": bool, "spin_backoff": bool}
_VARIANT_REF_KEYS = {"label", "sha256", "file"}
_VARIANT_KEYS = {"schema_version", "partitions", "disasm", "recomp", "function", "hook", "switch",
                 "d3d_block_on_time"}
_FUNCTION_KEYS = {"at", "note", "observed"}
_BLOCK_ON_TIME_KEYS = {"function": int, "device": int, "sem_off": int, "put_off": int}


@dataclass
class GameConfig:
    path: str
    variant: str
    label: str
    sha256: str
    title: Optional[str] = None
    disasm: Dict = field(default_factory=dict)
    recomp: Dict = field(default_factory=dict)          # exclude_manual already absolute
    partitions: Optional[str] = None                    # absolute
    seeds: List[Tuple[int, bool]] = field(default_factory=list)   # (address, observed)
    hooks: Dict = field(default_factory=dict)            # hooks_config.parse_list() result
    block_on_time: Optional[Dict] = None                 # [d3d_block_on_time]: function, device, sem_off, put_off


def _need_tomllib():
    if tomllib is None:
        raise RuntimeError(f"the game config (TOML) needs Python 3.11 or newer (tomllib); this is "
                           f"{sys.version.split()[0]}")


def _read(path):
    _need_tomllib()
    with open(path, "rb") as f:
        try:
            return tomllib.load(f)
        except tomllib.TOMLDecodeError as e:
            raise ValueError(f"{path}: {e}") from None


def _unknown(where, got, allowed):
    extra = sorted(set(got) - set(allowed))
    if extra:
        raise ValueError(f"{where}: unknown key(s) {extra}; allowed: {sorted(allowed)}")


def _schema(path, doc):
    v = doc.get("schema_version")
    if v not in SCHEMA_VERSIONS:
        raise ValueError(f"{path}: schema_version {v!r} is not supported by this toolkit "
                         f"(supported: {sorted(SCHEMA_VERSIONS)}) - update the toolkit or the file")


def _typed(where, table, allowed):
    _unknown(where, table, allowed)
    for k, v in table.items():
        t = allowed[k]
        if not isinstance(v, t) or (t is int and isinstance(v, bool)):
            raise ValueError(f"{where}.{k}: expected {t.__name__}, got {v!r}")
    return dict(table)


def _rel(base_file, p):
    return os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(base_file)), p))


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load(path, xbe_path):
    """The config for this XBE: the common part merged with its variant's part."""
    doc = _read(path)
    _schema(path, doc)
    _unknown(path, doc, _COMMON_KEYS)
    game = doc.get("game", {})
    _unknown(f"{path} [game]", game, _GAME_KEYS)
    disasm = _typed(f"{path} [disasm]", doc.get("disasm", {}), _DISASM_KEYS)
    recomp = _typed(f"{path} [recomp]", doc.get("recomp", {}), _RECOMP_KEYS)
    if "exclude_manual" in recomp:
        recomp["exclude_manual"] = _rel(path, recomp["exclude_manual"])

    variants = doc.get("variant", {})
    if not variants:
        raise ValueError(f"{path}: no [variant.<id>] table")
    for vid, ref in variants.items():
        _unknown(f"{path} [variant.{vid}]", ref, _VARIANT_REF_KEYS)
        for k in _VARIANT_REF_KEYS:
            if not isinstance(ref.get(k), str) or not ref[k]:
                raise ValueError(f"{path} [variant.{vid}]: {k} must be a non-empty string")
    digest = sha256_of(xbe_path)
    match = [vid for vid, ref in variants.items() if ref["sha256"].lower() == digest]
    if not match:
        known = ", ".join(f"{vid} ({ref['label']})" for vid, ref in variants.items())
        raise ValueError(f"{xbe_path}: SHA-256 {digest} matches no variant in {path} (known: {known})")
    vid = match[0]
    ref = variants[vid]
    vpath = _rel(path, ref["file"])

    vdoc = _read(vpath)
    _schema(vpath, vdoc)
    _unknown(vpath, vdoc, _VARIANT_KEYS)
    disasm.update(_typed(f"{vpath} [disasm]", vdoc.get("disasm", {}), _DISASM_KEYS))
    vrecomp = _typed(f"{vpath} [recomp]", vdoc.get("recomp", {}), _RECOMP_KEYS)
    if "exclude_manual" in vrecomp:
        vrecomp["exclude_manual"] = _rel(vpath, vrecomp["exclude_manual"])
    recomp.update(vrecomp)
    if vdoc.get("switch"):
        raise ValueError(f"{vpath}: [[switch]] is reserved - the lifter has no switch-table override yet")

    seeds = []
    for e in vdoc.get("function", []):
        _unknown(f"{vpath} [[function]]", e, _FUNCTION_KEYS)
        at = e.get("at")
        if isinstance(at, bool) or not isinstance(at, int) or not 0 <= at <= 0xFFFFFFFF:
            raise ValueError(f"{vpath} [[function]]: at must be a 32-bit address, got {at!r}")
        if "note" in e and not isinstance(e["note"], str):
            raise ValueError(f"{vpath} [[function]] at 0x{at:08X}: note must be a string")
        if "observed" in e and not isinstance(e["observed"], bool):
            raise ValueError(f"{vpath} [[function]] at 0x{at:08X}: observed must be true or false")
        seeds.append((at, bool(e.get("observed", False))))

    from tools.recomp import hooks_config
    hooks = hooks_config.parse_list(vdoc.get("hook", []))
    partitions = vdoc.get("partitions")
    if partitions is not None and not isinstance(partitions, str):
        raise ValueError(f"{vpath}: partitions must be a path string")

    block_on_time = None
    if "d3d_block_on_time" in vdoc:
        block_on_time = _typed(f"{vpath} [d3d_block_on_time]", vdoc["d3d_block_on_time"], _BLOCK_ON_TIME_KEYS)
        missing = sorted(set(_BLOCK_ON_TIME_KEYS) - set(block_on_time))
        if missing:
            raise ValueError(f"{vpath} [d3d_block_on_time]: missing {missing}")
        for k in ("function", "device"):
            if not 0 < block_on_time[k] <= 0xFFFFFFFF:
                raise ValueError(f"{vpath} [d3d_block_on_time].{k}: a 32-bit address, got {block_on_time[k]!r}")
        for k in ("sem_off", "put_off"):
            if not 0 <= block_on_time[k] < 0x10000:
                raise ValueError(f"{vpath} [d3d_block_on_time].{k}: a device field offset, got {block_on_time[k]!r}")

    return GameConfig(path=os.path.abspath(path), variant=vid, label=ref["label"], sha256=digest,
                      title=game.get("title"), disasm=disasm, recomp=recomp,
                      partitions=_rel(vpath, partitions) if partitions else None,
                      seeds=seeds, hooks=hooks, block_on_time=block_on_time)


def describe(cfg):
    return (f"game config {os.path.basename(cfg.path)}: variant {cfg.variant} ({cfg.label}), "
            f"{len(cfg.seeds)} seeds, {sum(map(len, cfg.hooks.values()))} hooks"
            + (f", partitions {os.path.basename(cfg.partitions)}" if cfg.partitions else "")
            + (f", D3D BlockOnTime 0x{cfg.block_on_time['function']:08X}" if cfg.block_on_time else ""))
