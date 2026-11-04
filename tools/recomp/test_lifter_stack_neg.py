"""U1 lifter fixes from sp00n v0.14/v0.15, checked on the lifter's own text and on recomp_types.h:

- neg negates in unsigned arithmetic (cebfc64): -(int32_t)0x80000000 is signed overflow, which clang -O2 may
  miscompile;
- 16-bit push / pop move esp by 2 (114193f): `pop cx` used to emit POP32(esp, cx), which names no C variable, and
  `push cx` pushed 4 bytes;
- POP32 writes its destination after esp moves (54b64ca): `pop esp` leaves esp at the popped value.
"""
import os
import re
import unittest

from .disasm import Disassembler
from .lifter import Lifter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def _lift(code_hex):
    code = bytes.fromhex(code_hex)
    insns = Disassembler().disassemble_function(code, 0x1000, 0x1000 + len(code))
    lifter = Lifter()
    return [s for i in insns for s in lifter.lift_instruction(i)]


class Neg(unittest.TestCase):
    def test_unsigned_negation(self):
        body = " ".join(_lift("f7d8"))          # neg eax
        self.assertIn("0u - (uint32_t)", body)
        self.assertNotIn("-(int32_t)", body)


class PushPop16(unittest.TestCase):
    def test_word_register_push_and_pop(self):
        body = " ".join(_lift("6651" "665a"))   # push cx; pop dx
        self.assertNotIn("PUSH32(esp, LO16(ecx))", body)
        self.assertNotIn("POP32(esp, dx)", body)
        self.assertIn("esp -= 2; MEM16(esp) = (uint16_t)(LO16(ecx));", body)
        self.assertIn("esp += 2;", body)
        self.assertIn("SET_LO16(edx, _tmp)", body)

    def test_word_memory_pop_writes_after_the_increment(self):
        body = " ".join(_lift("668f07"))        # pop word [edi]
        i_inc, i_write = body.index("esp += 2;"), body.index("MEM16(edi")
        self.assertLess(i_inc, i_write)

    def test_dword_push_pop_unchanged(self):
        body = " ".join(_lift("51" "58"))       # push ecx; pop eax
        self.assertIn("PUSH32(esp, ecx);", body)
        self.assertIn("POP32(esp, eax);", body)


class Pop32Order(unittest.TestCase):
    def test_destination_written_after_sp_moves(self):
        with open(os.path.join(ROOT, "templates", "runtime", "recomp_types.h"), encoding="utf-8") as f:
            text = f.read()
        m = re.search(r"#define POP32\(sp, dst\) do \{(.*?)\} while\(0\)", text, re.S)
        self.assertIsNotNone(m)
        body = m.group(1)
        self.assertLess(body.index("(sp) += 4"), body.index("(dst) ="))


if __name__ == "__main__":
    unittest.main()
