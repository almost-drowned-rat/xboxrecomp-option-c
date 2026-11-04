"""Tests for tools/hot_path_io.py: the standing rule 'no blocking log/file I/O in kernel-call, DPC, ISR or
gate-holding paths' (maintainer, 2026-10-27) holds for this tree, and the lint sees what it is for."""
import os
import re
import shutil

from tools import hot_path_io as h

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def test_tree_is_clean():
    found = h.check_repo(ROOT)
    assert found == [], "\n".join("%s:%d: %s" % f for f in found)


def test_header_redirects_every_name_the_lint_relies_on():
    text = open(os.path.join(ROOT, "src/kernel/log_async.h"), encoding="utf-8").read()
    defined = set(re.findall(r"#define\s+(\w+)\(\.\.\.\)\s+xbox_log_", text))
    assert defined == set(h.REDIRECTED)


def test_bypasses_are_flagged():
    src = ("#undef fprintf\n"
           "void f(void) { (fprintf)(stderr, \"x\"); OutputDebugStringA(\"x\"); __mingw_fprintf(stderr, \"x\");"
           " _write(2, \"x\", 1); WriteFile(GetStdHandle(STD_ERROR_HANDLE), 0, 0, 0, 0); }\n")
    found = h.check_source("src/kernel/x.c", src, allowed={})
    assert [line for _, line, _ in found] == [1, 2, 2, 2, 2, 2]


def test_file_open_needs_an_entry_with_a_reason():
    src = "static int dpc(void)\n{\n    FILE *f = fopen(\"x\", \"w\");\n    return f != 0;\n}\n"
    found = h.check_source("src/kernel/x.c", src, allowed={})
    assert len(found) == 1 and "dpc()" in found[0][2] and found[0][1] == 3
    assert h.check_source("src/kernel/x.c", src, allowed={("src/kernel/x.c", "dpc"): "test"}) == []


def test_comments_and_strings_do_not_count():
    src = ("/* fopen(\"a\") #undef fprintf */\n// OutputDebugStringA(\"x\")\n"
           "static void f(void) { const char *s = \"fopen(x) (fprintf)(\"; (void)s; }\n")
    assert h.check_source("src/kernel/x.c", src, allowed={}) == []


def test_a_library_without_the_cmake_call_is_flagged(tmp_path):
    for cm in h.HOT_LIBS:
        os.makedirs(tmp_path / os.path.dirname(cm), exist_ok=True)
        shutil.copy(os.path.join(ROOT, cm), tmp_path / cm)
    usb = tmp_path / "src/usb/CMakeLists.txt"
    usb.write_text(usb.read_text(encoding="utf-8").replace("xboxrecomp_hot_path_logging(xbox_usb)",
                                                           "# xboxrecomp_hot_path_logging(xbox_usb)"),
                   encoding="utf-8")
    found = [f for f in h.check_repo(str(tmp_path)) if f[0].endswith("CMakeLists.txt")]
    assert found == [("src/usb/CMakeLists.txt", 0, "does not call xboxrecomp_hot_path_logging(xbox_usb)")]


def test_gate_api_outside_the_hot_libraries_is_flagged(tmp_path):
    for cm in h.HOT_LIBS:
        os.makedirs(tmp_path / os.path.dirname(cm), exist_ok=True)
        shutil.copy(os.path.join(ROOT, cm), tmp_path / cm)
    os.makedirs(tmp_path / "src/video", exist_ok=True)
    (tmp_path / "src/video/new_isr.c").write_text("void isr(void) { xbox_set_irq_line(3, 1); }\n")
    found = [f for f in h.check_repo(str(tmp_path)) if f[0] == "src/video/new_isr.c"]
    assert len(found) == 1 and "not in a hot-path library" in found[0][2]
