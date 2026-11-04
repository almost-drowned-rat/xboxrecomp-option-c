"""x86 fences lift to C11 fences of the same strength (option C, F2).

They used to lift to nothing. On x86-64 that is almost true (only mfence does anything), on ARM64 it is wrong:
another core may then see the pushbuffer stores after D3D's kickoff write that sfence orders them before.
sfence -> release, lfence -> acquire, mfence -> seq_cst (recomp_types.h RECOMP_FENCE_*), and none of them is
reported as unimplemented. Cache hints (prefetch*) stay no-ops.
"""
import unittest

from .disasm import Instruction
from .lifter import Lifter


def _lift(mnemonic):
    lifter = Lifter()
    insn = Instruction(0x00012340, 3, mnemonic, "", "", operands=[])
    return lifter, " ".join(lifter.lift_instruction(insn))


class FenceTest(unittest.TestCase):

    def test_each_fence_has_its_strength(self):
        for mnemonic, macro in (("sfence", "RECOMP_FENCE_STORE()"), ("lfence", "RECOMP_FENCE_LOAD()"),
                                ("mfence", "RECOMP_FENCE_FULL()")):
            with self.subTest(mnemonic=mnemonic):
                lifter, out = _lift(mnemonic)
                self.assertIn(macro, out)
                self.assertNotIn("RECOMP_UNIMPL", out)
                self.assertEqual(lifter.unimplemented, {})

    def test_cache_hint_stays_a_no_op(self):
        lifter, out = _lift("prefetchnta")
        self.assertNotIn("RECOMP_FENCE", out)
        self.assertNotIn("RECOMP_UNIMPL", out)


if __name__ == "__main__":
    unittest.main()
