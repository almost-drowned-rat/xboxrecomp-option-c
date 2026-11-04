"""x87 cases at precision control 24 bit (the mode Xbox titles run in; option-C gate G1).

The harness runs every "fpu" case at PC=53 (harness.FP_CONTROL_WORD), where the double-backed model and the
hardware agree by construction. Titles do not: Burnout 3 loads control word 0x003F (PC=24, round to nearest, all
exceptions masked), and the x87 then rounds the significand of every add/sub/mul/div/sqrt result to 24 bits while
keeping the extended exponent range. So each case here switches the precision itself, first thing, exactly as game
code does: `fldcw` from a word in the scratch buffer. The real CPU then computes at PC=24, and the lifted side sees
the same `fldcw` and must round the same way (RECOMP_FP_PC / recomp_fp_round24) - or the case fails.

Results are compared bit-exact (tol 0). Compares mask fnstsw to the condition codes (C0/C2/C3, 0x4500): the
exception flags (precision!) are set by inexact results and are not modelled.

Run: py -3 -m tools.conformance.pc24 [-k substr] [--keep] (needs vcvars32, like the main suite).
"""
import struct

from .cases import Case

# mov/fldcw through [eax+56]: the last free bytes of the scratch buffer, away from the operands at [eax] and
# [eax+16] and the store area at [eax+32..48].
PC24 = ["mov word ptr [eax+56], 003Fh", "fldcw word ptr [eax+56]"]
PC53 = ["mov word ptr [eax+58], 027Fh", "fldcw word ptr [eax+58]"]
EQ_FLAGS = ["fnstsw ax", "and eax, 04500h"]


def f32(x: float) -> float:
    """x rounded to float (round to nearest even), as a double."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


# --- inputs (double pairs at [eax] and [eax+16]) ---------------------------------------------------------------
# The Burnout 3 draw-list case: a float scale (1/255 as stored by the game) times an integer weight. At PC=24 the
# product is already a float, so it equals its own float-stored copy; at double precision it does not, and the
# sorted-list re-insert walked between the value and its copy forever (race-start freeze).
_BO3 = [(f32(1 / 255), 96.0), (f32(1 / 255), 200.0), (f32(1 / 3), 3.0), (f32(0.1), 7.0), (f32(1 / 60), 37.0),
        (f32(0.7), f32(0.3)), (f32(2.2), 13.0)]

# Ordinary operands whose exact results need rounding at 24 bits.
_ROUND = [(1.0, 3.0), (2.0, 3.0), (0.1, 0.2), (f32(0.1), f32(0.2)), (1e10, 3.0), (100.0, 7.0), (-5.5, 0.3),
          (16777217.0, 1.0), (3.0, 1e-7)]

# Ties at 24 bits: exact results halfway between two floats, round to even.
_T = 2.0 ** -24
_TIES = [(1.0, _T), (1.0, 3 * _T), (1.0 + 2 * _T, _T), (-1.0, -_T), (16777216.0, 1.0), (16777216.0, 3.0)]

# Outside float's range: the x87 keeps the 15-bit exponent at PC=24 - no overflow to inf, no flush/denormal. A
# model that rounds through (float) gets inf/0 here.
_RANGE = [(1e30, 1e30), (-1e30, 1e30), (1e-30, 1e-30), (1e-20, 1e-20), (3.0e38, 10.0), (1.5e-38, 0.25),
          (1e200, 1e-100)]

# Square roots (fsqrt takes st0 only; the second operand is unused).
_SQRT = [(2.0, 0.0), (3.0, 0.0), (10.0, 0.0), (1e-40, 0.0), (1e60, 0.0), (f32(0.1), 0.0)]


def _bin(op):
    return PC24 + ["fld qword ptr [eax]", f"{op} qword ptr [eax+16]"]


CASES_PC24 = [
    Case("pc24_bo3_product_equals_stored_copy",
         "Burnout 3 draw list: a PC=24 product equals its float-stored copy (fst/fcomp, C3 set, C0 clear)",
         PC24 + ["fld qword ptr [eax]", "fmul qword ptr [eax+16]", "fst dword ptr [eax+32]",
                 "fcomp dword ptr [eax+32]"] + EQ_FLAGS, _BO3, "fpu"),
    Case("pc24_bo3_walk_neither_greater_nor_less",
         "the re-insert walk's tests from the other side: copy vs. v in registers is neither < nor > at PC=24",
         PC24 + ["fld qword ptr [eax]", "fmul qword ptr [eax+16]", "fst dword ptr [eax+32]",
                 "fld dword ptr [eax+32]", "fcomp st(1)"] + EQ_FLAGS, _BO3, "fpu"),
    Case("pc24_fadd", "fadd result rounded to 24 bits", _bin("fadd"), _ROUND + _TIES, "fpu"),
    Case("pc24_fsub", "fsub result rounded to 24 bits", _bin("fsub"), _ROUND + _TIES, "fpu"),
    Case("pc24_fmul", "fmul result rounded to 24 bits", _bin("fmul"), _ROUND + _BO3, "fpu"),
    Case("pc24_fdiv", "fdiv result rounded to 24 bits", _bin("fdiv"), _ROUND, "fpu"),
    Case("pc24_fdivr", "fdivr (reversed) rounded to 24 bits", _bin("fdivr"), _ROUND, "fpu"),
    Case("pc24_ties_to_even", "exact halfway results round to even at 24 bits", _bin("fadd"), _TIES, "fpu"),
    Case("pc24_range_mul", "PC=24 keeps the extended exponent: no inf, no flush outside float's range",
         _bin("fmul"), _RANGE, "fpu"),
    Case("pc24_range_div", "same for fdiv", _bin("fdiv"), _RANGE, "fpu"),
    Case("pc24_fsqrt", "fsqrt rounded to 24 bits, also outside float's range",
         PC24 + ["fld qword ptr [eax]", "fsqrt"], _SQRT, "fpu"),
    Case("pc24_load_is_exact", "fld does not round: PC applies to arithmetic results only",
         PC24 + ["fld qword ptr [eax]", "fld qword ptr [eax+16]"], _ROUND, "fpu"),
    Case("pc24_fild_add",
         "fild loads exactly, the add rounds: 2^24+1 + 0 = 2^24 at PC=24",
         PC24 + ["mov dword ptr [eax+48], 01000001h", "fild dword ptr [eax+48]", "fadd qword ptr [eax+16]"],
         [(0.0, 0.0), (0.0, 1.0), (0.0, -1.0)], "fpu"),
    Case("pc24_chain", "every step of a chain rounds: ((a*b)+a)/b, sqrt",
         PC24 + ["fld qword ptr [eax]", "fmul qword ptr [eax+16]", "fadd qword ptr [eax]",
                 "fdiv qword ptr [eax+16]", "fsqrt"],
         [(1.0, 3.0), (f32(0.1), 7.0), (2.0, 3.0), (100.0, 7.0), (f32(1 / 255), 96.0)], "fpu"),
    Case("pc24_then_pc53", "precision follows the control word: the same product at PC=24, then at PC=53",
         PC24 + ["fld qword ptr [eax]", "fmul qword ptr [eax+16]"]
         + PC53 + ["fld qword ptr [eax]", "fmul qword ptr [eax+16]"],
         _ROUND + _BO3, "fpu"),
    Case("pc24_stack_ops_keep_value", "fxch/fld st(i) move values without rounding",
         PC24 + ["fld qword ptr [eax]", "fld qword ptr [eax+16]", "fxch st(1)", "fld st(1)"], _ROUND, "fpu"),
]
