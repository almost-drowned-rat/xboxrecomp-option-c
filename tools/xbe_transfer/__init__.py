"""xbe_transfer - carry addresses from one build of a game to another (T1, e.g. Burnout 3 US -> PAL).

Two XBEs of the same game (regions, revisions) share almost all code, moved by a few hundred bytes. Seeds,
overrides and hooks written for one build name addresses of that build. This package matches the functions of the
source build in the target by their bytes with every address operand masked, derives data addresses from the
operands of matched code, and rewrites address lists and C sources. What it cannot match is reported, never guessed.

    py -3 -m tools.xbe_transfer match --src-xbe us.xbe --src-functions functions.json --dst-xbe pal.xbe -o map.json
    py -3 -m tools.xbe_transfer remap --map map.json --out-dir out/ seed_functions.json recomp_manual.c hooks.toml

Modules: image (sections of an XBE), mask (masked signature of a function), match (function and data map),
remap (rewrite files).
"""
