"""A completed host reduction must clear the x87 C2 polling bit (sp00n f69a435).

The CRT reduces with `again: fprem; fnstsw ax; test ah, 4; jnz again`. The
lift always finishes the reduction in one step but left C2 as an earlier fxam
had set it, so the loop never ended.

Upstream's test compiles the emitted line against stand-in globals; option-c's
x87 state is in the register set, so this one compiles in ctx form against
recomp_types.h, with the translator's fp_* stack macros.
"""
import pytest

from .disasm import Instruction
from .lifter import Lifter
from .test_lifter_eflags_snapshot import _PRELUDE, _build_and_run, _function


def _source(mnemonic, emitted, expected):
    return _PRELUDE + _function("reduce", emitted) + """
int main(void) {
    X86Ctx c; X86Ctx *ctx = &c;
    for (unsigned top = 0; top < 8; top++) {
        memset(&c, 0, sizeof c);
        g_fp_top = top;
        g_fp_stack[top] = 8.0; g_fp_stack[(top + 1) & 7] = 3.0;
        g_fp_cc = 0x4500; /* includes FXAM's C2; unrelated bits retained */
        unsigned polls = 0;
        do { reduce(ctx, 0); } while ((g_fp_cc & 0x0400) && ++polls < 3);
        if (polls || g_fp_cc != EXPECTED_CC || g_fp_top != top ||
            g_fp_stack[(top + 1) & 7] != 3.0 || g_fp_stack[top] != EXPECTED) return 1;
    }
    return 0;
}
""".replace("EXPECTED_CC", expected[1]).replace("EXPECTED", repr(expected[0]))


# Since sp00n 999a8c6 the status word after a complete reduction is the
# quotient's low bits in C0/C3/C1 and C2 clear: 8/3 chopped is 2 (C3), to
# nearest 3 (C3|C1). Upstream's f69a435 expectation, 0x4100 (C2 cleared, the
# other bits kept), predates that.
@pytest.mark.parametrize("mnemonic,expected", [("fprem", (2.0, "0x4000")),
                                               ("fprem1", (-1.0, "0x4200"))])
def test_completed_remainder_releases_c2_polling(mnemonic, expected):
    emitted = "\n".join(Lifter().lift_instruction(Instruction(0, 2, mnemonic, "", "")))
    source = _source(mnemonic, emitted, expected)
    ran = _build_and_run(source)
    assert ran.returncode == 0, ran.stdout + ran.stderr
    # Negative control: the lift before f69a435, which left C2 alone, must
    # fail the same bounded loop.
    fn = "fmod" if mnemonic == "fprem" else "remainder"
    old = source.replace(emitted, f"fp_top() = {fn}(fp_top(), fp_st1());")
    assert old != source
    assert _build_and_run(old).returncode == 1
