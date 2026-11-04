"""Alias entries (T2): a second entry point inside another function, lifted as its own C function.

C cannot jump into the middle of another function, so the disassembler emits an alias as a Function of
its own that runs from the entry address to the end of the host function (`_build_alias_entries`,
detection method `tail_jump_alias`); the translator lifts it like any other function. The shared tail
is therefore present twice in the generated code: inline in the host and as the alias's body. Both
read every guest register from the ctx at entry -- there are no host-function locals an alias could
inherit -- so an alias sees exactly the registers its caller left, as on the CPU.

This test lifts a host with an alias at its loop head, compiles both with the real runtime header at
-O0 and -O2 and calls each entry:
    host:   mov ecx, 7          ; only the host sets ecx
    alias:  add eax, ecx        ; loop head, entry of the alias
            dec edx
            jnz alias
            ret
host(eax=0, edx=3) = 21; alias(eax=0, ecx=5, edx=3) = 15 -- the alias must use the caller's ecx, not
the host's 7. Both pop the return address (esp + 4). The host's body keeps the loop inline (it does not
call the alias), and the alias is a separate symbol.
"""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

from . import config
from .translator import FunctionTranslator

ROOT = Path(__file__).resolve().parents[2]
BASE = 0x10000
ALIAS = BASE + 5
IMAGE = bytes.fromhex("b907000000" "01c8" "4a" "75fb" "c3")
END = BASE + len(IMAGE)

HARNESS = r"""
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <stdio.h>

ptrdiff_t g_xbox_mem_offset;
static uint8_t g_mem[0x10000];

BODY

static int call(void (*fn)(RECOMP_PARAMS), uint32_t eax_in, uint32_t ecx_in, uint32_t edx_in,
                uint32_t want, const char *what) {
    X86Ctx c = {0};
    X86Ctx *ctx = &c;
    g_eax = eax_in; g_ecx = ecx_in; g_edx = edx_in; g_esp = 0x8000;
    fn(ctx, g_mem);
    if (g_eax != want || g_esp != 0x8004 || g_edx != 0) {
        printf("FAIL %s: eax=%u (want %u) esp=%08X edx=%u\n", what, g_eax, want, g_esp, g_edx);
        return 1;
    }
    return 0;
}

int main(void) {
    int bad = 0;
    bad |= call(host, 0, 0xDEAD, 3, 21, "host");
    bad |= call(alias, 0, 5, 3, 15, "alias with the caller's ecx");
    bad |= call(host, 0, 5, 3, 21, "host after the alias (no state kept between calls)");
    bad |= call(alias, 100, 1, 1, 101, "alias, one iteration");
    if (!bad) printf("OK\n");
    return bad;
}
"""


@pytest.fixture(autouse=True)
def _restore_layout(monkeypatch):
    # config._install sets module globals; put them back so later tests see the unconfigured fallback
    for name in ("_SECTIONS", "SECTIONS", "_configured_from", "TEXT_VA_START", "TEXT_VA_END", "RDATA_VA_START",
                 "RDATA_VA_END", "DATA_VA_START", "DATA_VA_END", "KERNEL_THUNK_ADDR", "ENTRY_POINT"):
        monkeypatch.setattr(config, name, getattr(config, name))


def _cc():
    return shutil.which("clang") or shutil.which("gcc")


def _bodies():
    config._install([config.Section(".text", BASE, 0x100, 0, 0x100, True)],
                    entry_point=BASE, kernel_thunk_addr=BASE, origin="alias-entry")
    db = {
        BASE: {"start": hex(BASE), "end": END, "_addr": BASE, "size": END - BASE, "section": ".text",
               "detection_method": "prologue"},
        # what _build_alias_entries emits: from the entry to the host's end
        ALIAS: {"start": hex(ALIAS), "end": END, "_addr": ALIAS, "size": END - ALIAS, "section": ".text",
                "detection_method": "tail_jump_alias"},
    }
    tr = FunctionTranslator(IMAGE.ljust(0x100, b"\xcc"), db)
    return tr.translate_function(BASE, db[BASE]), tr.translate_function(ALIAS, db[ALIAS])


def test_host_keeps_the_shared_tail_inline_and_the_alias_is_its_own_function():
    host, alias = _bodies()
    assert "void sub_00010000(RECOMP_PARAMS)" in host, host
    assert "void sub_00010005(RECOMP_PARAMS)" in alias, alias
    assert "sub_00010005(" not in host.split("RECOMP_PARAMS)", 1)[1], "the host must not call the alias"
    assert "ecx = 7" in host.replace("0x7", "7") and "ecx = 7" not in alias.replace("0x7", "7")


@pytest.mark.parametrize("opt", ["-O0", "-O2"])
def test_calls_via_the_alias_and_via_the_host_both_return_correctly(opt):
    cc = _cc()
    if not cc:
        pytest.skip("no C compiler on PATH")
    host, alias = _bodies()
    body = host.replace("sub_00010000", "host") + "\n" + alias.replace("sub_00010005", "alias")
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "alias.c"
        exe = Path(tmp) / "alias.exe"
        c.write_text(HARNESS.replace("BODY", body), encoding="utf-8")
        built = subprocess.run([cc, opt, "-w", "-I", str(ROOT / "templates" / "runtime"), str(c), "-o", str(exe)],
                               capture_output=True, text=True)
        assert built.returncode == 0, built.stderr[-3000:]
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        assert ran.returncode == 0 and "OK" in ran.stdout, ran.stdout + ran.stderr
