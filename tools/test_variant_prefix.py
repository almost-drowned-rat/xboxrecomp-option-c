"""tools/variant_prefix.py: every sub_ name of a lift and the fixed names get the prefix; the header compiles
with two versions' copies of the same function in one program (clang, if on PATH); unchanged output keeps the
file; a bad prefix is refused."""
import os
import shutil
import subprocess
import sys
import tempfile

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import variant_prefix  # noqa: E402


def lift(d, body):
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "recomp_funcs.h"), "w") as f:
        f.write("void sub_00011000(void);\nvoid sub_00011000_gen(void);\nvoid xbe_entry_point(void);\n")
    with open(os.path.join(d, "recomp_0000.c"), "w") as f:
        f.write(body)


def test_names_and_fixed():
    with tempfile.TemporaryDirectory() as t:
        lift(t, "void sub_00011000_gen(void) { sub_0002ABCD(); }\nvoid sub_00011100_enter(void) {}\n")
        out, n, changed = variant_prefix.write(t, "pal_a_")
        text = open(out).read()
        assert changed and n == 4
        for name in ("sub_00011000", "sub_00011000_gen", "sub_0002ABCD", "sub_00011100_enter"):
            assert f"#define {name} pal_a_{name}" in text
        for name in variant_prefix.FIXED:
            assert f"#define {name} pal_a_{name}" in text
        assert '#define RECOMP_VARIANT_PREFIX "pal_a_"' in text
        mtime = os.path.getmtime(out)
        _, _, changed = variant_prefix.write(t, "pal_a_")
        assert not changed and os.path.getmtime(out) == mtime


def test_bad_prefix_and_empty():
    with tempfile.TemporaryDirectory() as t:
        lift(t, "")
        with pytest.raises(SystemExit):
            variant_prefix.write(t, "1bad")
    with tempfile.TemporaryDirectory() as t:
        with pytest.raises(SystemExit):
            variant_prefix.write(t, "us_")


@pytest.mark.skipif(not shutil.which("clang"), reason="clang not on PATH")
def test_two_versions_link():
    """The same function name in two versions' lifts links into one program and each call stays in its version."""
    with tempfile.TemporaryDirectory() as t:
        src = "int sub_00011000(void) { return VERSION; }\nint recomp_lookup(void) { return sub_00011000(); }\n"
        objs = []
        for v, val in (("us", 1), ("pal_a", 2)):
            d = os.path.join(t, v)
            lift(d, src.replace("VERSION", str(val)))
            hdr, _, _ = variant_prefix.write(d, f"{v}_")
            o = os.path.join(t, f"{v}.o")
            subprocess.run(["clang", "-c", "-include", hdr, os.path.join(d, "recomp_0000.c"), "-o", o], check=True)
            objs.append(o)
        main = os.path.join(t, "main.c")
        with open(main, "w") as f:
            f.write("int us_recomp_lookup(void); int pal_a_recomp_lookup(void);\n"
                    "int main(void) { return us_recomp_lookup() == 1 && pal_a_recomp_lookup() == 2 ? 0 : 1; }\n")
        exe = os.path.join(t, "t.exe")
        subprocess.run(["clang", main, *objs, "-o", exe], check=True)
        assert subprocess.run([exe]).returncode == 0
