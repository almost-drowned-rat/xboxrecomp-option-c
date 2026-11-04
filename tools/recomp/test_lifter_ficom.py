"""ficom/ficomp compare st0 with a signed integer in memory (sp00n 2d38bcd).

They fell through to the `/* FPU: ... */` catch-all: the status word kept the
previous compare and ficomp never popped. Upstream checks it with its fpdiff
harness (v0.14 infrastructure option-c does not have); here the lifted
`ficom(p) [ecx]; fnstsw ax` is compiled in ctx form against recomp_types.h.
"""
import pytest

from .disasm import BasicBlock, Instruction, Operand
from .lifter import Lifter, lift_basic_block
from .test_lifter_eflags_snapshot import _PRELUDE, _build_and_run, _function


def _lift(m, size):
    a = Instruction(0, 2, m, "", "")
    a.operands = [Operand(type="mem", mem_base="ecx", mem_size=size)]
    b = Instruction(2, 2, "fnstsw", "ax", "")
    b.operands = [Operand(type="reg", reg="ax")]
    lines, _ = lift_basic_block(Lifter(), BasicBlock(0, [a, b]))
    return "\n".join("    " + line for line in lines)


@pytest.mark.parametrize("m", ["ficom", "ficomp"])
@pytest.mark.parametrize("size", [2, 4])
def test_ficom_compares_against_the_signed_integer(m, size):
    body = _lift(m, size)
    assert "FPU:" not in body, body
    itype = "int16_t" if size == 2 else "int32_t"
    source = _PRELUDE + _function("run", body) + """
static uint8_t g_mem[16];
int main(void) {
    const double st[] = {-40000.0, -32768.0, -1.5, -1.0, -0.0, 0.0, 0.5, 1.0,
                         32767.0, 40000.0, 3e9, INFINITY, -INFINITY, NAN};
    const ITYPE iv[] = {-32768, -1, 0, 1, 32767};
    X86Ctx c; X86Ctx *ctx = &c;
    for (unsigned i = 0; i < sizeof st / sizeof st[0]; i++)
    for (unsigned j = 0; j < sizeof iv / sizeof iv[0]; j++) {
        double a = st[i], b = (double)iv[j];
        unsigned want = (a != a) ? 0x4500u : a < b ? 0x0100u : a > b ? 0u : 0x4000u;
        memset(&c, 0, sizeof c);
        memcpy(g_mem, &iv[j], sizeof iv[j]);
        g_ecx = 0; g_fp_top = 7; g_fp_stack[7] = a;
        g_fp_cc = 0x4500u;                   /* a stale "unordered" */
        run(ctx, g_mem);
        if ((g_eax & 0x4500u) != want) return 1;
        if (g_fp_top != POPPED) return 2;
    }
    return 0;
}
""".replace("ITYPE", itype).replace("POPPED", "0" if m == "ficomp" else "7")
    ran = _build_and_run(source)
    assert ran.returncode == 0, f"check {ran.returncode} failed\n" + ran.stdout + ran.stderr
