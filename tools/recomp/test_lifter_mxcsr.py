"""MXCSR is guest state, and the SSE integer conversions round by it
(sp00n c50c597, Kitae Noh).

STMXCSR and LDMXCSR fell to RECOMP_UNIMPL, so a store of MXCSR wrote nothing.
Jet Set Radio Future runs one every frame: a routine stores MXCSR, tests the
rounding-control bits and returns E_UNEXPECTED unless they say "nearest" --
with nothing stored, the answer was whatever that memory happened to hold.
Burnout 3 stores and reloads it at 0x001DD63F/0x001DD652.

CVTSS2SI/CVTSD2SI were lifted as a C cast, which truncates (and is undefined
out of range), where the hardware rounds by MXCSR.RC -- nearest-even unless
the title changed it -- and answers out-of-range values with the integer
indefinite. Only the CVTT forms truncate.

Option C: upstream's thread-local g_mxcsr is the X86Ctx field mxcsr here
(g_mxcsr is a macro over the ctx in scope), reset to 0x1F80 by X86CTX_INIT.
The emitted text is checked, the helper is compiled and run (a text check
cannot tell a correct nearest-even from one that rounds halves up), and a
lifted stmxcsr/ldmxcsr/cvt sequence runs in ctx form against recomp_types.h.
"""
from pathlib import Path

from .disasm import BasicBlock, Instruction, Operand
from .lifter import Lifter, _EFLAGS_PRESERVE, lift_basic_block
from .test_lifter_eflags_snapshot import _PRELUDE, _build_and_run, _function

_ROOT = Path(__file__).resolve().parents[2]


def _insn(mnemonic, op_str, operands):
    instruction = Instruction(0, 4, mnemonic, op_str, "")
    instruction.operands = operands
    return instruction


def _mem(base=None, disp=0, size=4):
    return Operand(type="mem", mem_base=base, mem_disp=disp, mem_size=size)


def _reg(name):
    return Operand(type="reg", reg=name)


# ── Emitted text ──

def test_store_writes_the_guest_mxcsr():
    assert Lifter().lift_instruction(
        _insn("stmxcsr", "dword ptr [0x265150]", [_mem(disp=0x265150)])) == [
        "MEM32(0x265150) = g_mxcsr; /* stmxcsr dword ptr [0x265150] */"]


def test_load_reads_the_guest_mxcsr():
    assert Lifter().lift_instruction(
        _insn("ldmxcsr", "dword ptr [esp + 4]", [_mem("esp", 4)])) == [
        "g_mxcsr = MEM32(esp + 4); /* ldmxcsr dword ptr [esp + 4] */"]


def test_neither_changes_eflags():
    assert "stmxcsr" in _EFLAGS_PRESERVE
    assert "ldmxcsr" in _EFLAGS_PRESERVE


def _lift_cvt(mnemonic, source):
    op_str = "eax, " + source.reg if source.type == "reg" else "eax, [ecx]"
    return "\n".join(Lifter().lift_instruction(
        _insn(mnemonic, op_str, [_reg("eax"), source])))


def test_rounding_forms_follow_mxcsr():
    out = _lift_cvt("cvtss2si", _reg("xmm1"))
    assert "recomp_cvt_si32((double)xmm1.f[0], g_mxcsr, 0)" in out, out
    out = _lift_cvt("cvtsd2si", _reg("xmm2"))
    assert "recomp_cvt_si32(xmm2.d[0], g_mxcsr, 0)" in out, out


def test_truncating_forms_truncate():
    out = _lift_cvt("cvttss2si", _mem("ecx", size=4))
    assert "recomp_cvt_si32((double)MEMF(ecx), g_mxcsr, 1)" in out, out
    out = _lift_cvt("cvttsd2si", _mem("ecx", size=8))
    assert "recomp_cvt_si32(MEMD(ecx), g_mxcsr, 1)" in out, out


def test_no_bare_cast_remains():
    for mnemonic in ("cvtss2si", "cvttss2si", "cvtsd2si", "cvttsd2si"):
        out = _lift_cvt(mnemonic, _reg("xmm0"))
        assert "(int32_t)xmm0" not in out, (mnemonic, out)


# ── The state lives in the ctx, reset like the x87 control word ──

def test_mxcsr_is_a_ctx_field_reset_to_0x1f80():
    types = (_ROOT / "templates" / "runtime" / "recomp_types.h").read_text(encoding="utf-8")
    layout = (_ROOT / "src" / "kernel" / "xbox_memory_layout.h").read_text(encoding="utf-8")
    for text in (types, layout):
        assert "uint32_t  mxcsr;" in text
        assert "#define g_mxcsr           (ctx->mxcsr)" in text
        assert "RECOMP_TLS uint32_t g_mxcsr" not in text
    init = layout[layout.index("#define X86CTX_INIT"):]
    init = init[:init.index("}") + 1]
    assert ".mxcsr = 0x1F80u" in init, init
    assert "static inline int32_t recomp_cvt_si32(double value, uint32_t mxcsr, int truncate)" in types


def test_conformance_harnesses_reset_mxcsr():
    conf = _ROOT / "tools" / "conformance"
    for name in ("corpus_run.py", "harness.py", "xbe_run.py"):
        text = (conf / name).read_text(encoding="utf-8")
        assert ".fp_cc = 0x4000, .mxcsr = 0x1F80 }" in text, name
        assert "g_fp_control_word = 0x027Fu; g_mxcsr = 0x1F80u;" in text, name


# ── Compiled ──

def test_conversions_round_like_the_hardware():
    source = _PRELUDE + r'''
#define NEAR 0x1F80u   /* the reset value: RC=00 */
#define DOWN 0x3F80u   /* RC=01 */
#define UP   0x5F80u   /* RC=10 */
#define CHOP 0x7F80u   /* RC=11 */

int main(void) {
    /* Nearest-even, not truncation: the defect. */
    if (recomp_cvt_si32(2.7, NEAR, 0) != 3) return __LINE__;
    if (recomp_cvt_si32(-2.7, NEAR, 0) != -3) return __LINE__;
    if (recomp_cvt_si32(2.5, NEAR, 0) != 2) return __LINE__;
    if (recomp_cvt_si32(3.5, NEAR, 0) != 4) return __LINE__;
    /* The other rounding controls. */
    if (recomp_cvt_si32(2.7, DOWN, 0) != 2) return __LINE__;
    if (recomp_cvt_si32(-2.2, DOWN, 0) != -3) return __LINE__;
    if (recomp_cvt_si32(2.2, UP, 0) != 3) return __LINE__;
    if (recomp_cvt_si32(-2.7, CHOP, 0) != -2) return __LINE__;
    /* The truncating form ignores RC. */
    if (recomp_cvt_si32(2.7, NEAR, 1) != 2) return __LINE__;
    if (recomp_cvt_si32(-2.7, UP, 1) != -2) return __LINE__;
    /* The integer indefinite. */
    if (recomp_cvt_si32((double)NAN, NEAR, 0) != INT32_MIN) return __LINE__;
    if (recomp_cvt_si32((double)INFINITY, NEAR, 1) != INT32_MIN) return __LINE__;
    if (recomp_cvt_si32(3e9, NEAR, 0) != INT32_MIN) return __LINE__;
    if (recomp_cvt_si32(-3e9, NEAR, 1) != INT32_MIN) return __LINE__;
    /* The ends of the range still convert. */
    if (recomp_cvt_si32(2147483647.0, NEAR, 0) != INT32_MAX) return __LINE__;
    if (recomp_cvt_si32(-2147483648.0, NEAR, 1) != INT32_MIN) return __LINE__;
    if (recomp_cvt_si32(2147483647.4, NEAR, 0) != INT32_MAX) return __LINE__;
    return 0;
}
'''
    ran = _build_and_run(source)
    assert ran.returncode == 0, f"check on line {ran.returncode} failed\n" + ran.stdout + ran.stderr


def _lift_block(items):
    for k, item in enumerate(items):
        item.address = k
    lines, _ = lift_basic_block(Lifter(), BasicBlock(0, items))
    return "\n".join("    " + line for line in lines)


def test_lifted_sequence_keeps_mxcsr_in_the_ctx():
    # stmxcsr [ecx]      -- the reset value
    # ldmxcsr [ecx + 4]  -- RC=down
    # cvtss2si eax, xmm1 / cvtsd2si edx, xmm2 / cvttss2si ebx, xmm1
    # stmxcsr [ecx + 8]  -- what was loaded
    body = _lift_block([
        _insn("stmxcsr", "dword ptr [ecx]", [_mem("ecx")]),
        _insn("ldmxcsr", "dword ptr [ecx + 4]", [_mem("ecx", 4)]),
        _insn("cvtss2si", "eax, xmm1", [_reg("eax"), _reg("xmm1")]),
        _insn("cvtsd2si", "edx, xmm2", [_reg("edx"), _reg("xmm2")]),
        _insn("cvttss2si", "ebx, xmm3", [_reg("ebx"), _reg("xmm3")]),
        _insn("stmxcsr", "dword ptr [ecx + 8]", [_mem("ecx", 8)]),
    ])
    assert "RECOMP_UNIMPL" not in body, body
    source = _PRELUDE + _function("run", body) + r'''
static uint8_t g_mem[64];
int main(void) {
    X86Ctx c; X86Ctx *ctx = &c;
    uint32_t stored, reloaded, down = 0x3F80u;
    memset(&c, 0, sizeof c);
    g_mxcsr = 0x1F80u;                       /* what X86CTX_INIT sets */
    memset(g_mem, 0xCC, sizeof g_mem);
    memcpy(g_mem + 4, &down, 4);
    g_ecx = 0;
    g_xmm1.f[0] = 2.7f;
    g_xmm2.d[0] = -2.2;                      /* a double in lane 0 */
    g_xmm3.f[0] = -2.7f;
    run(ctx, g_mem);
    memcpy(&stored, g_mem, 4); memcpy(&reloaded, g_mem + 8, 4);
    if (stored != 0x1F80u) return 1;         /* the reset value was stored */
    if (g_mxcsr != 0x3F80u) return 2;        /* ldmxcsr reached the ctx */
    if (reloaded != 0x3F80u) return 3;
    if ((int32_t)g_eax != 2) return 4;       /* 2.7 rounded down */
    if ((int32_t)g_edx != -3) return 5;      /* -2.2 rounded down, read as a double */
    if ((int32_t)g_ebx != -2) return 6;      /* truncated regardless of RC */
    return 0;
}
'''
    ran = _build_and_run(source)
    assert ran.returncode == 0, f"check {ran.returncode} failed\n" + ran.stdout + ran.stderr
