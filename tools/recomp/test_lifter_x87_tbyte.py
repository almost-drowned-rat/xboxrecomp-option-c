"""fld/fstp tbyte convert between the 80-bit format and the double stack
(sp00n b40805c).

fld tbyte read the first four bytes as a float, so 1.0L loaded as 0.0, and
fstp tbyte was a comment that stored nothing and left the value on the stack.
The CRT's libm loads its constants this way. Upstream checks it with its
fpdiff harness (v0.14 infrastructure option-c does not have); here the lifted
pair is compiled in ctx form against recomp_types.h and run over known
extended-precision encodings.
"""
from .disasm import BasicBlock, Instruction, Operand
from .lifter import Lifter, lift_basic_block
from .test_lifter_eflags_snapshot import _PRELUDE, _build_and_run, _function


def _tbyte(base):
    return Operand(type="mem", mem_base=base, mem_size=10)


def _lift(items):
    for k, item in enumerate(items):
        item.address = k
    lines, _ = lift_basic_block(Lifter(), BasicBlock(0, items))
    return "\n".join("    " + line for line in lines)


def _insn(m, op, op_str):
    item = Instruction(0, 2, m, op_str, "")
    item.operands = [op]
    return item


def test_lifted_text():
    load = _lift([_insn("fld", _tbyte("ecx"), "xword ptr [ecx]")])
    store = _lift([_insn("fstp", _tbyte("edx"), "xword ptr [edx]")])
    assert "recomp_f80_load(" in load and "MEMF" not in load, load
    assert "recomp_f80_store(fp_top()" in store and "fp_pop();" in store, store


def test_round_trip_and_values():
    body = _lift([_insn("fld", _tbyte("ecx"), "xword ptr [ecx]"),
                  _insn("fstp", _tbyte("edx"), "xword ptr [edx]")])
    source = _PRELUDE + _function("run", body) + r"""
#include <stdio.h>
static uint8_t g_mem[64];
struct f80 { uint64_t mant; uint16_t se; double value; };
int main(void) {
    /* Encodings whose value a double holds exactly, so the store gives the
     * same ten bytes back. */
    const struct f80 cases[] = {
        {0x8000000000000000ull, 0x3FFF, 1.0},
        {0x8000000000000000ull, 0xBFFF, -1.0},
        {0xA000000000000000ull, 0xC000, -2.5},
        {0xC90FDAA22168C000ull, 0x4000, 3.141592653589793115997963},
        {0x8000000000000000ull, 0x3FFF - 1074, 0x1p-1074},
        {0xFFFFFFFFFFFFF800ull, 0x3FFF + 1023, 0x1.fffffffffffffp1023},
        {0, 0x0000, 0.0},
        {0, 0x8000, -0.0},
        {0x8000000000000000ull, 0x7FFF, INFINITY},
        {0x8000000000000000ull, 0xFFFF, -INFINITY},
    };
    X86Ctx c; X86Ctx *ctx = &c;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memset(&c, 0, sizeof c); memset(g_mem, 0xCC, sizeof g_mem);
        memcpy(g_mem, &cases[i].mant, 8); memcpy(g_mem + 8, &cases[i].se, 2);
        g_ecx = 0; g_edx = 32;
        g_fp_top = 0; g_fp_stack[7] = 12345.0;
        run(ctx, g_mem);
        if (g_fp_top != 0) return 10 + i;                 /* pushed, popped */
        if (g_fp_stack[7] != cases[i].value ||
            signbit(g_fp_stack[7]) != signbit(cases[i].value)) {
            printf("load %u: %a\n", i, g_fp_stack[7]); return 30 + i;
        }
        if (memcmp(g_mem + 32, g_mem, 10)) return 50 + i;  /* same bytes back */
        if (g_mem[42] != 0xCC) return 70 + i;              /* ten bytes, no more */
    }
    /* A NaN stays a NaN through both directions. */
    {
        uint64_t mant = 0xC000000000000000ull; uint16_t se = 0x7FFF;
        memset(&c, 0, sizeof c);
        memcpy(g_mem, &mant, 8); memcpy(g_mem + 8, &se, 2);
        g_ecx = 0; g_edx = 32;
        run(ctx, g_mem);
        if (!isnan(g_fp_stack[7])) return 90;
        memcpy(&mant, g_mem + 32, 8); memcpy(&se, g_mem + 40, 2);
        if ((se & 0x7FFF) != 0x7FFF || !(mant << 1)) return 91;
    }
    return 0;
}
"""
    ran = _build_and_run(source)
    assert ran.returncode == 0, f"check {ran.returncode} failed\n" + ran.stdout + ran.stderr
