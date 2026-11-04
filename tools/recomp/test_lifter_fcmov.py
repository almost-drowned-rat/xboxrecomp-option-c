"""FCMOVcc lifting: the conditional x87 move must happen (sp00n 01064f3).

The eight FCMOVcc forms were left as comments (`/* FPU: fcmove ... */`), so the
move never happened. MSVC compiles float min/max to

    fld a; fld b; fcom st(1); fnstsw ax; test ah, 1;
    fcmove st(0), st(1); fxch st(1); fstp st(0)

and without the move min(x, 1.0) always returned 1.0 and max(x, 0.0) always
0.0. Jet Set Radio Future clamps its screen-fade colors that way, so the fade
went back to zero every frame and the boot never left its first logo.

Each form reads the flags of the jcc with the same condition code. The text
tests are upstream's; the compiled one is option-c's, in ctx form against
recomp_types.h.
"""

from tools.recomp.disasm import BasicBlock, Instruction, Operand
from tools.recomp.lifter import Lifter, lift_basic_block
from tools.recomp.test_lifter_eflags_snapshot import (
    _PRELUDE, _build_and_run, _function)


def _reg(name, size=4):
    op = Operand("reg")
    op.reg = name
    op.size = size
    return op


def _imm(value, size=1):
    op = Operand("imm")
    op.imm = value
    op.size = size
    return op


def _insn(address, mnemonic, op_str, operands, size=2):
    insn = Instruction(address, size, mnemonic, op_str, "00" * size)
    insn.operands = operands
    return insn


def _fcmov(mnemonic, source=1, address=0):
    return _insn(address, mnemonic, f"st(0), st({source})",
                 [_reg("st(0)", 10), _reg(f"st({source})", 10)])


def _lift_block(instructions):
    lifter = Lifter()
    lifter.func_start = 0
    lifter.func_end = 0x40
    generated, _ = lift_basic_block(
        lifter, BasicBlock(start=0, instructions=instructions))
    return "\n".join(generated)


def _min_idiom():
    return [
        _insn(0, "fcom", "st(1)", [_reg("st(1)", 10)]),
        _insn(2, "fnstsw", "ax", [_reg("ax", 2)]),
        _insn(4, "test", "ah, 1", [_reg("ah", 1), _imm(1)], size=3),
        _fcmov("fcmove", 1, address=7),
    ]


def test_msvc_min_idiom_moves_under_the_test_flags():
    body = _lift_block(_min_idiom())
    move = [line for line in body.splitlines() if "fcmove" in line]
    assert move, body
    assert "FPU: fcmove" not in body, body
    # je after `test ah, 1` is ZF = ((ah & 1) == 0).
    assert move[0].startswith("if (") and "fp_top() = fp_st1();" in move[0], move[0]
    assert "TEST_Z" in move[0] or "== 0" in move[0], move[0]


def test_every_form_moves_its_source_register():
    for mnemonic in ("fcmovb", "fcmove", "fcmovbe", "fcmovu",
                     "fcmovnb", "fcmovne", "fcmovnbe", "fcmovnu"):
        block = [
            _insn(0, "test", "ah, 0x41", [_reg("ah", 1), _imm(0x41)], size=3),
            _fcmov(mnemonic, 3, address=3),
        ]
        body = _lift_block(block)
        assert "FPU: fcmov" not in body, (mnemonic, body)
        assert "fp_top() = g_fp_stack[(g_fp_top + 3) & 7];" in body, (mnemonic, body)


def test_parity_forms_read_parity():
    block = [
        _insn(0, "test", "ah, 0x44", [_reg("ah", 1), _imm(0x44)], size=3),
        _fcmov("fcmovu", 1, address=3),
    ]
    assert "RECOMP_PARITY8" in _lift_block(block)


def test_untracked_flags_fall_back_to_flags_variable():
    body = "\n".join(Lifter().lift_instruction(_fcmov("fcmovne", 2)))
    assert body.startswith("if (_flags") and "g_fp_stack[(g_fp_top + 2) & 7]" in body, body


def test_compiled_min_idiom_keeps_the_smaller_value():
    """st0 = b, st1 = a; `fcom st(1); fnstsw ax; test ah, 1; fcmove` leaves
    min(a, b) in st0 (C0 is st0 < st1; ZF after the test is its negation)."""
    body = _lift_block(_min_idiom())
    source = _PRELUDE + _function("run", body) + """
int main(void) {
    const double v[] = {1, 2, -1, 0, -0.0, 1e300, -1e-300};
    X86Ctx c; X86Ctx *ctx = &c;
    for (unsigned i = 0; i < 7; i++) for (unsigned j = 0; j < 7; j++) {
        double a = v[i], b = v[j];
        memset(&c, 0, sizeof c);
        g_fp_top = 6; g_fp_stack[6] = b; g_fp_stack[7] = a;
        run(ctx, 0);
        if (g_fp_top != 6 || g_fp_stack[7] != a) return 1;
        if (memcmp(&g_fp_stack[6], (b < a) ? &b : &a, sizeof(double))) return 2;
    }
    return 0;
}
"""
    ran = _build_and_run(source)
    assert ran.returncode == 0, f"check {ran.returncode} failed\n" + ran.stdout + ran.stderr
