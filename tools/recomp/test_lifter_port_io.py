"""`in` / `out` lift to the runtime's port I/O (sp00n 46aee91, #194), not RECOMP_UNIMPL: D3D's vblank ISR reads the
field pin at 0x80C0 and a stale accumulator said "odd field" for ever (Burnout 3's XMV intro)."""
import unittest

from .disasm import Disassembler
from .lifter import Lifter


def _lift(code_hex):
    code = bytes.fromhex(code_hex)
    insns = Disassembler().disassemble_function(code, 0x1000, 0x1000 + len(code))
    lifter = Lifter()
    return [s for i in insns for s in lifter.lift_instruction(i)]


class PortIo(unittest.TestCase):
    def test_in_al_dx(self):
        body = " ".join(_lift("ec"))            # in al, dx
        self.assertIn("SET_LO8(eax, xbox_PortIn((edx & 0xFFFFu), 1))", body)
        self.assertNotIn("UNIMPL", body)

    def test_in_eax_imm(self):
        body = " ".join(_lift("e5c0"))          # in eax, 0xc0
        self.assertIn("xbox_PortIn(0xC0u, 4)", body)

    def test_out_dx_al(self):
        body = " ".join(_lift("ee"))            # out dx, al
        self.assertIn("xbox_PortOut((edx & 0xFFFFu), (uint32_t)(LO8(eax)), 1);", body)

    def test_out_imm_ax(self):
        body = " ".join(_lift("66e780"))        # out 0x80, ax
        self.assertIn("xbox_PortOut(0x80u, (uint32_t)(LO16(eax)), 2);", body)


if __name__ == "__main__":
    unittest.main()
