"""x87 trig, remainder, scale and exp/log edge cases (sp00n 999a8c6).

fsin/fcos/fsincos/fptan ignored the 2^63 range limit (C2 set, operand
unchanged), and fptan pushed 1.0 after a NaN. fprem/fprem1 always said
complete and never set C0/C3/C1, so the CRT's reduction loops and octant picks
read stale bits; fprem chopped through fmod without its quotient. fscale cast
huge or infinite scales to int, f2xm1 and fyl2xp1 lost precision near zero,
and fisttp rounded under the control word.

Upstream checks these with its fpdiff harness (v0.14 infrastructure option-c
does not have). Here every lift is compiled in ctx form against recomp_types.h
-- recomp_fp_trig_in_range and recomp_fprem take ctx, because the condition
bits they write are ctx->fp_cc -- and checked against the architectural
definitions.
"""
from .disasm import Instruction, Operand
from .lifter import Lifter
from .test_lifter_eflags_snapshot import _PRELUDE, _build_and_run, _function


def _lift(m, ops=()):
    insn = Instruction(0, 2, m, "", "")
    insn.operands = list(ops)
    return "\n".join("    " + line for line in Lifter().lift_instruction(insn))


def test_lifted_text_passes_ctx_to_the_status_writers():
    for m in ("fsin", "fcos", "fsincos", "fptan"):
        assert "recomp_fp_trig_in_range(ctx, fp_top())" in _lift(m), m
    assert "recomp_fprem(ctx, fp_top(), fp_st1(), 0)" in _lift("fprem")
    assert "recomp_fprem(ctx, fp_top(), fp_st1(), 1)" in _lift("fprem1")
    assert "recomp_fscale(fp_top(), fp_st1())" in _lift("fscale")


def test_compiled_edge_cases():
    mem = Operand(type="mem", mem_base="ecx", mem_size=4)
    funcs = {m: _lift(m) for m in ("fsin", "fcos", "fsincos", "fptan", "fprem",
                                   "fprem1", "fscale", "f2xm1", "fyl2xp1")}
    funcs["fisttp"] = _lift("fisttp", [mem])
    source = _PRELUDE + "".join(_function("op_" + m, body)
                                for m, body in funcs.items()) + r"""
#include <stdio.h>
static uint8_t g_mem[16];
static X86Ctx c;
#define CTX (&c)
static void load(double st0, double st1) {
    memset(&c, 0, sizeof c);
    c.fp_top = 6; c.fp_stack[6] = st0; c.fp_stack[7] = st1;
}
#define ST0 (c.fp_stack[c.fp_top & 7])
#define ST1 (c.fp_stack[(c.fp_top + 1) & 7])
#define C2 (c.fp_cc & 0x0400u)
static int close_to(double got, double want) {
    return fabs(got - want) <= fabs(want) * 1e-12;
}
int main(void) {
    void (*trig[])(RECOMP_PARAMS) = {op_fsin, op_fcos, op_fsincos, op_fptan};
    const double huge[] = {0x1p63, -0x1p63, 1e300};
    /* Out of range: C2 set, operand and depth unchanged. */
    for (unsigned t = 0; t < 4; t++) for (unsigned h = 0; h < 3; h++) {
        load(huge[h], 7.0);
        trig[t](CTX, 0);
        if (!C2 || c.fp_top != 6 || ST0 != huge[h]) return 1;
    }
    /* In range: C2 cleared, the result computed. */
    for (unsigned t = 0; t < 4; t++) {
        load(0.5, 7.0); c.fp_cc = 0x0400u;
        trig[t](CTX, 0);
        if (C2) return 2;
    }
    load(0.5, 7.0); op_fsin(CTX, 0); if (ST0 != sin(0.5)) return 3;
    load(0.5, 7.0); op_fsincos(CTX, 0);
    if (c.fp_top != 5 || ST0 != cos(0.5) || ST1 != sin(0.5)) return 4;
    /* fptan of a NaN or an infinity pushes a NaN, not 1.0. */
    load(NAN, 7.0); op_fptan(CTX, 0); if (c.fp_top != 5 || !isnan(ST0)) return 5;
    load(INFINITY, 7.0); op_fptan(CTX, 0); if (c.fp_top != 5 || !isnan(ST0)) return 6;
    load(0.5, 7.0); op_fptan(CTX, 0); if (c.fp_top != 5 || ST0 != 1.0) return 7;

    /* fprem chops, fprem1 rounds to nearest; C0/C3/C1 = quotient bits 2/1/0. */
    {
        const double pairs[][2] = {{8, 3}, {7.5, 3}, {-7.5, 3}, {7.5, -3},
                                   {100, 7}, {2.5, 2}, {1, 3}, {29, 2}, {-29, 2},
                                   {1e10, 1.25}, {0.0, 3}};
        for (unsigned i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
        for (int nearest = 0; nearest < 2; nearest++) {
            double a = pairs[i][0], b = pairs[i][1];
            double want = nearest ? remainder(a, b) : fmod(a, b);
            double qd = fabs((a - want) / b);   /* exact for these inputs */
            unsigned q = (unsigned)fmod(qd, 8.0);
            unsigned cc = ((q & 4u) << 6) | ((q & 2u) << 13) | ((q & 1u) << 9);
            load(a, b); c.fp_cc = 0x4500u;
            (nearest ? op_fprem1 : op_fprem)(CTX, 0);
            if (ST0 != want || signbit(ST0) != signbit(want)) {
                printf("%u/%d: %a want %a\n", i, nearest, ST0, want); return 10;
            }
            if (c.fp_cc != cc) {
                printf("%u/%d: cc %04X want %04X\n", i, nearest, c.fp_cc, cc); return 11;
            }
            if (c.fp_top != 6 || ST1 != b) return 12;
        }
    }
    /* A huge quotient reduces in parts: C2 stays set until the last step, and
     * the guest's loop ends at fmod's answer. */
    {
        unsigned steps = 0;
        load(1e300, 3.0);
        do { op_fprem(CTX, 0); } while (C2 && ++steps < 100);
        if (steps == 0 || steps >= 100 || ST0 != fmod(1e300, 3.0)) return 13;
    }

    /* fscale: trunc(st1), the infinite scales, no int overflow. */
    load(1.5, 3.7);    op_fscale(CTX, 0); if (ST0 != 12.0) return 20;
    load(1.5, -3.7);   op_fscale(CTX, 0); if (ST0 != 1.5 / 8) return 21;
    load(1.5, 1e10);   op_fscale(CTX, 0); if (ST0 != INFINITY) return 22;
    load(-1.5, 1e10);  op_fscale(CTX, 0); if (ST0 != -INFINITY) return 23;
    load(1.5, -1e10);  op_fscale(CTX, 0); if (ST0 != 0.0) return 24;
    load(0.0, INFINITY);       op_fscale(CTX, 0); if (!isnan(ST0)) return 25;
    load(INFINITY, -INFINITY); op_fscale(CTX, 0); if (!isnan(ST0)) return 26;
    load(-2.0, -INFINITY);     op_fscale(CTX, 0); if (ST0 != 0.0 || !signbit(ST0)) return 27;
    load(NAN, 1.0);            op_fscale(CTX, 0); if (!isnan(ST0)) return 28;
    if (c.fp_top != 6) return 29;

    /* f2xm1 and fyl2xp1 keep the low bits near zero. */
    load(1e-20, 0.0); op_f2xm1(CTX, 0);
    if (!close_to(ST0, 1e-20 * 0.69314718055994530942)) return 30;
    load(1e-20, 2.0); op_fyl2xp1(CTX, 0);
    if (c.fp_top != 7 || !close_to(ST0, 2.0 * 1e-20 / 0.69314718055994530942)) return 31;

    /* fisttp chops whatever RC says, and pops. */
    {
        const double v[] = {2.7, -2.7, 2.5, -0.5};
        const int32_t want[] = {2, -2, 2, 0};
        for (unsigned i = 0; i < 4; i++) {
            int32_t got;
            load(v[i], 9.0); c.fp_control_word = 0x037Fu; c.r_ecx = 0;
            op_fisttp(CTX, g_mem);
            memcpy(&got, g_mem, 4);
            if (got != want[i] || c.fp_top != 7) return 40 + i;
        }
    }
    return 0;
}
"""
    ran = _build_and_run(source)
    assert ran.returncode == 0, f"check {ran.returncode} failed\n" + ran.stdout + ran.stderr
