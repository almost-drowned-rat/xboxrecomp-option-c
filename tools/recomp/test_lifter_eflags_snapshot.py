"""sahf and the fcomi family leave EFLAGS in the _fa snapshot (sp00n bb2b5c7).

sahf was a comment, so a jcc/setcc after it read g_fp_cmp -- the last x87
compare, not what AH held: `fxam; fnstsw ax; sahf` or an AH edit before the
sahf answered a question the guest never asked. fcomi wrote g_fp_cmp, which an
fcom between it and its reader replaced. Upstream checks this with its fpdiff
harness (tools/recomp/fpdiff.py, v0.14 infrastructure option-c does not have);
here the lifted blocks are compiled in ctx form against recomp_types.h and
checked against the architectural flag definitions instead.
"""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

from .disasm import BasicBlock, Instruction, Operand
from .lifter import Lifter, lift_basic_block, _make_condition
from .translator import FP_STACK_MACROS, FP_STACK_UNDEFS

ROOT = Path(__file__).resolve().parents[2]


def _reg(name):
    return Operand(type="reg", reg=name)


def _insn(m, *ops, op_str=""):
    item = Instruction(0, 1, m, op_str, "")
    item.operands = list(ops)
    return item


def _block(items):
    for k, item in enumerate(items):
        item.address = k
    lines, _ = lift_basic_block(Lifter(), BasicBlock(0, items))
    return "\n".join("    " + line for line in lines)


def _build_and_run(source):
    cc = shutil.which("clang") or shutil.which("gcc")
    if not cc:
        pytest.skip("C compiler unavailable")
    with tempfile.TemporaryDirectory(prefix="eflags-snapshot-") as tmp:
        c, exe = Path(tmp) / "t.c", Path(tmp) / "t.exe"
        c.write_text(source, encoding="utf-8")
        built = subprocess.run(
            [cc, "-w", "-O2", "-I", str(ROOT / "templates" / "runtime"),
             str(c), "-o", str(exe)] + ([] if os.name == "nt" else ["-lm"]),
            capture_output=True, text=True)
        assert built.returncode == 0, built.stdout + built.stderr + source
        return subprocess.run([str(exe)], capture_output=True, text=True)


def _function(name, body):
    """A lifted body in ctx form: called as name(ctx, base), where base is the
    host address of guest address 0 (MEM* reads base + guest address)."""
    return (f"static void {name}(RECOMP_PARAMS) {{\n"
            "    (void)base;\n"
            "    uint32_t _fa = 0, _fb = 0; int32_t _fas = 0, _fbs = 0;\n"
            "    int _flags = 0, _cf = 0;\n"
            "    (void)_fa; (void)_fb; (void)_fas; (void)_fbs; (void)_flags; (void)_cf;\n"
            + "\n".join(FP_STACK_MACROS) + "\n" + body + "\n"
            + "\n".join(FP_STACK_UNDEFS) + "\n}\n")


_PRELUDE = """
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
"""

# setb bl, sete bh, setp cl, sets ch, seto dl -- CF ZF PF SF OF, one byte each.
_READERS = [("setb", "bl"), ("sete", "bh"), ("setp", "cl"), ("sets", "ch"),
            ("seto", "dl")]


def _readers():
    return [_insn(m, _reg(r)) for m, r in _READERS]


def test_sahf_loads_every_flag_from_ah_and_keeps_of():
    plain = _block([_insn("sahf")] + _readers())
    # inc ecx from 0x7FFFFFFF sets OF; sahf must keep it.
    after_inc = _block([_insn("inc", _reg("ecx")), _insn("sahf")] + _readers())
    assert "/* sahf - store AH" not in plain and "RECOMP_UNIMPL" not in plain, plain
    source = _PRELUDE + _function("plain", plain) + _function("after_inc", after_inc) + """
int main(void) {
    X86Ctx c; X86Ctx *ctx = &c;
    for (unsigned ah = 0; ah < 256; ah++) for (int of = 0; of < 2; of++) {
        memset(&c, 0, sizeof c);
        g_eax = 0xA5A500A5u | (ah << 8);
        g_ecx = of ? 0x7FFFFFFFu : 0;
        if (of) after_inc(ctx, 0); else plain(ctx, 0);
        if ((g_ebx & 0xFF) != (ah & 1)) return 1;               /* CF */
        if (((g_ebx >> 8) & 0xFF) != ((ah >> 6) & 1)) return 2; /* ZF */
        if ((g_ecx & 0xFF) != ((ah >> 2) & 1)) return 3;        /* PF */
        if (((g_ecx >> 8) & 0xFF) != ((ah >> 7) & 1)) return 4; /* SF */
        if ((g_edx & 0xFF) != (unsigned)of) return 5;           /* OF kept */
    }
    return 0;
}
"""
    ran = _build_and_run(source)
    assert ran.returncode == 0, f"check {ran.returncode} failed\n" + ran.stdout + ran.stderr


# Capstone's spellings (fcompi, not fcomip) with its single st(i) operand.
@pytest.mark.parametrize("mnemonic", ["fcomi", "fucomi", "fcompi", "fucompi"])
def test_fcomi_flags_survive_a_later_x87_compare(mnemonic):
    body = _block([_insn(mnemonic, _reg("st(1)"), op_str="st(1)"),
                   # A plain x87 compare in between: rewrites g_fp_cmp/g_fp_cc
                   # but not EFLAGS.
                   _insn("fcom", _reg("st(2)"), op_str="st(2)")] + _readers()[:4])
    source = _PRELUDE + _function("run", body) + """
int main(void) {
    const double v[] = {1, 2, -1, 0, -0.0, INFINITY, -INFINITY, NAN};
    X86Ctx c; X86Ctx *ctx = &c;
    for (unsigned i = 0; i < 8; i++) for (unsigned j = 0; j < 8; j++) {
        double a = v[i], b = v[j];
        int un = a != a || b != b;
        memset(&c, 0, sizeof c);
        g_fp_top = 5;
        g_fp_stack[5] = a; g_fp_stack[6] = b; g_fp_stack[7] = NAN; /* st2: unordered */
        run(ctx, 0);
        if ((g_ebx & 0xFF) != (unsigned)(un || a < b)) return 1;           /* CF */
        if (((g_ebx >> 8) & 0xFF) != (unsigned)(un || a == b)) return 2;   /* ZF */
        if ((g_ecx & 0xFF) != (unsigned)un) return 3;                      /* PF */
        if (((g_ecx >> 8) & 0xFF) != 0) return 4;                          /* SF clear */
        if (g_fp_top != POPPED) return 5;
    }
    return 0;
}
""".replace("POPPED", "6" if mnemonic.endswith("pi") else "5")
    ran = _build_and_run(source)
    assert ran.returncode == 0, f"check {ran.returncode} failed\n" + ran.stdout + ran.stderr
    # Negative control: the old lift, the ordering in g_fp_cmp read by the
    # conditions, must fail the same sweep because the fcom replaces it.
    old = Lifter._lift_fpu
    try:
        def legacy(self, insn, m, ops):
            if m == mnemonic:
                pop = " fp_pop();" if m.endswith("pi") else ""
                return [f"g_fp_cmp = RECOMP_FCMP(fp_top(), fp_st1());{pop}"]
            return old(self, insn, m, ops)
        Lifter._lift_fpu = legacy
        legacy_body = _block([_insn(mnemonic, _reg("st(1)"), op_str="st(1)"),
                              _insn("fcom", _reg("st(2)"), op_str="st(2)")]
                             + _readers()[:4])
    finally:
        Lifter._lift_fpu = old
    cmp_map = {"(_fa & 1u)": "(g_fp_cmp < 0 || g_fp_cmp == 2)",
               "(_fa & 0x40u)": "(g_fp_cmp == 0 || g_fp_cmp == 2)",
               "(_fa & 4u)": "(g_fp_cmp == 2)", "((_fa >> 7) & 1u)": "0"}
    for new, prior in cmp_map.items():
        legacy_body = legacy_body.replace(new, prior)
    assert _build_and_run(source.replace(body, legacy_body)).returncode != 0


def test_comiss_signed_conditions_read_sf_of_clear():
    ops = [_reg("xmm0"), _reg("xmm1")]
    assert _make_condition("jl", "comiss", ops)[0] == "0"
    assert _make_condition("jge", "comiss", ops)[0] == "1"
    assert _make_condition("js", "ucomiss", ops)[0] == "0"
    assert _make_condition("jno", "ucomiss", ops)[0] == "1"
    assert "_fca == _fcb" in _make_condition("jle", "comiss", ops)[0]
    assert "_fca != _fcb" in _make_condition("jg", "comiss", ops)[0]
