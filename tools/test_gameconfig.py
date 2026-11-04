"""Tests for tools/gameconfig.py (B1 item 3: per-game TOML, common part + one part per disc variant) and
tools/seeds_to_toml.py."""
import hashlib
import json

import pytest

from tools import gameconfig, seeds_to_toml


def _xbe(tmp_path, name, data):
    p = tmp_path / name
    p.write_bytes(data)
    return str(p), hashlib.sha256(data).hexdigest()


def _setup(tmp_path, common_extra="", us_extra="", pal_extra=""):
    us, us_sha = _xbe(tmp_path, "us.xbe", b"XBEH-us")
    pal, pal_sha = _xbe(tmp_path, "pal.xbe", b"XBEH-pal")
    cfg = tmp_path / "config"
    (cfg / "variants").mkdir(parents=True)
    (cfg / "game.toml").write_text(f'''schema_version = 1
[game]
title = "Test"
[recomp]
split = 1000
exclude_manual = "../src/manual.c"
{common_extra}
[variant.us]
label = "USA"
sha256 = "{us_sha}"
file = "variants/us.toml"
[variant.pal_a]
label = "Europe A"
sha256 = "{pal_sha.upper()}"
file = "variants/pal_a.toml"
''', encoding="utf-8")
    (cfg / "variants" / "us.toml").write_text(f'''schema_version = 1
partitions = "us.partitions.json"
[[function]]
at = 0x00012345
note = "seen"
observed = true
[[function]]
at = 0x00023456
[[hook]]
at = 0x00012350
call = "fix_us"
{us_extra}
''', encoding="utf-8")
    (cfg / "variants" / "pal_a.toml").write_text(f'''schema_version = 1
partitions = "pal_a.partitions.json"
[recomp]
split = 500
[[function]]
at = 0x00099999
{pal_extra}
''', encoding="utf-8")
    return str(cfg / "game.toml"), us, pal


def test_variant_chosen_by_sha256_with_common_part_and_relative_paths(tmp_path):
    game, us, pal = _setup(tmp_path)
    c = gameconfig.load(game, us)
    assert (c.variant, c.label, c.title) == ("us", "USA", "Test")
    assert c.seeds == [(0x12345, True), (0x23456, False)]
    assert c.recomp["split"] == 1000
    assert c.recomp["exclude_manual"] == str(tmp_path / "src" / "manual.c")
    assert c.partitions == str(tmp_path / "config" / "variants" / "us.partitions.json")
    assert list(c.hooks) == [0x12350] and c.hooks[0x12350][0]["call"] == "fix_us"
    p = gameconfig.load(game, pal)                  # upper-case hash in the file matches too
    assert p.variant == "pal_a" and p.seeds == [(0x99999, False)] and p.hooks == {}
    assert p.recomp["split"] == 500 and p.recomp["exclude_manual"].endswith("manual.c")   # override + common


def test_an_unknown_xbe_is_refused_with_the_known_variants(tmp_path):
    game, _, _ = _setup(tmp_path)
    other, sha = _xbe(tmp_path, "other.xbe", b"something else")
    with pytest.raises(ValueError, match=f"{sha}.*us \\(USA\\), pal_a \\(Europe A\\)"):
        gameconfig.load(game, other)


@pytest.mark.parametrize("where,extra", [
    ("common", "splitt = 3"),
    ("us", "[recomp]\nsplit = 1\nfoo = 2"),
    ("us", "[[function]]\nat = 0x1\nend = 0x2"),
    ("us", "seeds = 1"),
])
def test_unknown_keys_are_errors(tmp_path, where, extra):
    kw = {"common_extra": extra} if where == "common" else {"us_extra": extra}
    game, us, _ = _setup(tmp_path, **kw)
    with pytest.raises(ValueError, match="unknown key"):
        gameconfig.load(game, us)


def test_disasm_text_only_and_extra_sections_with_a_variant_override(tmp_path):
    """[disasm] text_only (Orta: executable-flagged data sections) next to extra_sections; a variant
    file overrides it."""
    game, us, pal = _setup(tmp_path, common_extra='[disasm]\ntext_only = true\nextra_sections = ["XIPS"]',
                           pal_extra='[disasm]\ntext_only = false')
    c = gameconfig.load(game, us)
    assert c.disasm == {"text_only": True, "extra_sections": ["XIPS"]}
    p = gameconfig.load(game, pal)
    assert p.disasm == {"text_only": False, "extra_sections": ["XIPS"]}
    (tmp_path / "b").mkdir()
    bad, us2, _ = _setup(tmp_path / "b", common_extra='[disasm]\ntext_only = "yes"')
    with pytest.raises(ValueError, match="text_only: expected bool"):
        gameconfig.load(bad, us2)


def test_wrong_types_are_errors(tmp_path):
    game, us, _ = _setup(tmp_path, us_extra='[recomp]\nsplit = "1000"')
    with pytest.raises(ValueError, match="split: expected int"):
        gameconfig.load(game, us)


def test_unknown_schema_version_is_refused(tmp_path):
    game, us, _ = _setup(tmp_path)
    text = open(game, encoding="utf-8").read().replace("schema_version = 1", "schema_version = 2", 1)
    open(game, "w", encoding="utf-8").write(text)
    with pytest.raises(ValueError, match="schema_version 2 is not supported"):
        gameconfig.load(game, us)


def test_switch_is_reserved(tmp_path):
    game, us, _ = _setup(tmp_path, us_extra="[[switch]]\nat = 0x1\ntargets = [0x2]")
    with pytest.raises(ValueError, match="reserved"):
        gameconfig.load(game, us)


def test_python_older_than_3_11_gets_a_clear_message(tmp_path, monkeypatch):
    game, us, _ = _setup(tmp_path)
    monkeypatch.setattr(gameconfig, "tomllib", None)
    with pytest.raises(RuntimeError, match="Python 3.11 or newer"):
        gameconfig.load(game, us)


def test_seeds_to_toml_keeps_every_seed_and_the_observed_reading_of_tools_disasm(tmp_path):
    seeds = [{"start": "0x001D2793", "note": 'thread start "A" observed at runtime; x'},
             {"start": "0x00354EA0", "note": "from the switch scan"},
             {"start": "0x00010000", "observed": True}]
    text = seeds_to_toml.convert(seeds)
    us, sha = _xbe(tmp_path, "us.xbe", b"x")
    (tmp_path / "v.toml").write_text("schema_version = 1\n" + text, encoding="utf-8")
    (tmp_path / "g.toml").write_text(f'schema_version = 1\n[variant.us]\nlabel = "U"\nsha256 = "{sha}"\n'
                                     f'file = "v.toml"\n', encoding="utf-8")
    c = gameconfig.load(str(tmp_path / "g.toml"), us)
    assert c.seeds == [(0x001D2793, True), (0x00354EA0, False), (0x00010000, True)]
    with pytest.raises(ValueError, match="unknown key"):
        seeds_to_toml.convert([{"start": "0x1", "end": "0x2"}])
    json.dumps(seeds)                                # input untouched
