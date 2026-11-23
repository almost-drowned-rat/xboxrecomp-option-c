# SPDX-License-Identifier: MIT
"""tools/ctx_rewrite.py on a small hand-written port file against a fake toolkit tree: definitions, prototypes and
calls get RECOMP_PARAMS / RECOMP_ARGS, register externs go, toolkit functions with ctx are called with it, a
ctx-only hook keeps (ctx, ...), and what needs a person lands in the REVIEW list."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "ctx_rewrite.py")

TOOLKIT_H = """\
void xdk_music_skip(RECOMP_PARAMS, int track);
void recomp_icall_fail_log(X86Ctx *ctx, uint32_t va);
"""

PORT_C = """\
#include "recomp_types.h"
extern uint32_t g_eax;
void sub_00012340(void);
static int helper(int x) { return x + (int)g_eax; }
static int caller(void) { return helper(2); }
void skip_music(void) { xdk_music_skip(3); }
void recomp_icall_fail_log(uint32_t va) { (void)va; }
void my_hook(void) { sub_00012340(); }
static void host_cb(void) { caller(); }
void install(void) { register_callback(host_cb); }
static int clash(void) { uint32_t base = g_ecx; return (int)base; }
/* a string with a call in it: "helper(1)" */
"""

HOOKS = 'call = "my_hook"\n'


def _setup(tmp_path):
    tk = tmp_path / "tk"
    (tk / "src").mkdir(parents=True)
    (tk / "templates" / "runtime").mkdir(parents=True)
    (tk / "src" / "xdk.h").write_text(TOOLKIT_H)
    port = tmp_path / "port.c"
    port.write_text(PORT_C)
    hooks = tmp_path / "hooks.toml"
    hooks.write_text(HOOKS)
    return tk, port, hooks


def _run(tk, port, hooks, write):
    args = [sys.executable, "-I", SCRIPT, "--toolkit", str(tk), "--hooks", str(hooks)]
    if write:
        args.append("--write")
    r = subprocess.run(args + [str(port)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return r.stdout


def test_dry_run_changes_nothing(tmp_path):
    tk, port, hooks = _setup(tmp_path)
    out = _run(tk, port, hooks, write=False)
    assert "(dry run)" in out
    assert port.read_text() == PORT_C


def test_rewrite(tmp_path):
    tk, port, hooks = _setup(tmp_path)
    out = _run(tk, port, hooks, write=True)
    src = port.read_text()
    assert "extern uint32_t g_eax;" not in src
    assert "void sub_00012340(RECOMP_PARAMS);" in src
    assert "static int helper(RECOMP_PARAMS, int x)" in src
    assert "return helper(RECOMP_ARGS, 2);" in src
    assert "static int caller(RECOMP_PARAMS)" in src
    assert "xdk_music_skip(RECOMP_ARGS, 3);" in src
    assert "void recomp_icall_fail_log(X86Ctx *ctx, uint32_t va)" in src
    assert "void my_hook(RECOMP_PARAMS) { sub_00012340(RECOMP_ARGS); }" in src
    assert '"helper(1)"' in src                      # strings are never rewritten
    review = out.split("REVIEW", 1)[1]
    assert "address of host_cb()" in review
    assert "own `base` declared" in review
    # a second run finds nothing left to do
    again = _run(tk, port, hooks, write=True)
    assert "rewritten: 0 definitions, 0 prototypes, 0 calls, 0 register externs" in again
    assert port.read_text() == src
