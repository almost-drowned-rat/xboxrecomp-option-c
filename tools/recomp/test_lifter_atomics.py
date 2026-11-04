"""lock xadd / lock cmpxchg are InterlockedIncrement and friends.

Both used to fall through to a TODO comment: a reference count that never
moved and a compare-and-swap that never swapped. That was harmless while every
guest thread ran synchronously, and stopped being harmless once the runtime
began spawning real host threads for a title's workers.

Lowering them to a plain read-modify-write would not do either: a racing
sequence is the exact bug these instructions exist to prevent.
"""
import unittest

from .disasm import BasicBlock, Instruction, Operand
from .lifter import Lifter, lift_basic_block


def _mem_ecx():
    return Operand(type="mem", mem_base="ecx", mem_index=None, mem_scale=1,
                   mem_disp=0, mem_size=4)


def _lift(mnemonic, src_reg="eax", lock=False):
    insn = Instruction(0, 3, mnemonic, "dword ptr [ecx], %s" % src_reg, "0fc101", lock=lock)
    insn.operands = [_mem_ecx(), Operand(type="reg", reg=src_reg)]
    lifted, _ = lift_basic_block(
        Lifter(), BasicBlock(start=0, instructions=[insn]))
    return chr(10).join(lifted)


class AtomicLiftTest(unittest.TestCase):
    def test_xadd_is_atomic_and_returns_the_old_value(self):
        generated = _lift("xadd", "edx")
        self.assertIn("RECOMP_ATOMIC_ADD32", generated)
        self.assertIn("_old", generated)
        self.assertNotIn("TODO", generated)

    def test_cmpxchg_is_a_real_compare_and_swap(self):
        generated = _lift("cmpxchg", "edx")
        self.assertIn("RECOMP_ATOMIC_CAS32", generated)
        # eax is only replaced when the comparand did not match.
        self.assertIn("if (_old != _cmp) eax = _old;", generated)
        self.assertNotIn("TODO", generated)

    def test_cmpxchg_snapshots_the_flags_before_touching_eax(self):
        # The ZF a following jz reads must come from the compare cmpxchg did,
        # not from eax after it may have been overwritten.
        generated = _lift("cmpxchg", "edx")
        cmp_at = generated.index("_fa = _old; _fb = _cmp;")
        eax_at = generated.index("if (_old != _cmp) eax = _old;")
        self.assertLess(cmp_at, eax_at)

    def test_lock_prefixed_forms_lift_the_same_way(self):
        # The decoder takes the prefix out of the mnemonic (Instruction.lock).
        for m in ("xadd", "cmpxchg"):
            with self.subTest(mnemonic=m):
                generated = _lift(m, "edx", lock=True)
                self.assertNotIn("TODO", generated)
                self.assertIn("RECOMP_ATOMIC_", generated)
                self.assertIn("/* lock " + m + " */", generated)

    def test_the_decoder_takes_the_lock_prefix_out_of_the_mnemonic(self):
        from tools.recomp.disasm import Disassembler
        d = Disassembler()
        insn = d._decode_instruction(next(d._cs.disasm(bytes.fromhex("f00fb14e20"), 0x1000)))
        self.assertEqual((insn.mnemonic, insn.lock), ("cmpxchg", True))
        insn = d._decode_instruction(next(d._cs.disasm(bytes.fromhex("0fb14e20"), 0x1000)))
        self.assertEqual((insn.mnemonic, insn.lock), ("cmpxchg", False))

    def test_flags_after_lock_cmpxchg_come_from_its_compare(self):
        # G1 (C gate): `lock cmpxchg; jz` read a stale _flags and took the
        # wrong branch whenever the exchange happened.
        from tools.recomp.lifter import _make_condition
        insn = Instruction(0, 5, "cmpxchg", "dword ptr [esi + 0x20], ecx", "f00fb14e20", lock=True)
        insn.operands = [_mem_ecx(), Operand(type="reg", reg="ecx")]
        jz = Instruction(5, 2, "je", "0x10", "7409")
        jz.jump_target = 0x10
        lifted, _ = lift_basic_block(Lifter(), BasicBlock(start=0, instructions=[insn, jz]))
        text = chr(10).join(lifted)
        self.assertNotIn("_flags", text)
        self.assertIn("_fa", text.split("lock cmpxchg")[1])

    def test_locked_read_modify_writes_are_never_lifted_plain(self):
        # lock inc: a CAS loop around the plain lift (G1 gap 1), no plain store.
        insn = Instruction(0, 4, "inc", "dword ptr [ecx]", "f0ff01", lock=True)
        insn.operands = [_mem_ecx()]
        lifted, _ = lift_basic_block(Lifter(), BasicBlock(start=0, instructions=[insn]))
        text = chr(10).join(lifted)
        self.assertIn("RECOMP_ATOMIC_CAS_OK32(_lk_p, _lk_o, _lk_v)", text)
        self.assertNotIn("MEM32(ecx) = ", text)
        # a locked form without an atomic lift stays RECOMP_UNIMPL
        insn = Instruction(0, 5, "bts", "dword ptr [ecx], 3", "f00fba2903", lock=True)
        insn.operands = [_mem_ecx(), Operand(type="imm", imm=3)]
        lifted, _ = lift_basic_block(Lifter(), BasicBlock(start=0, instructions=[insn]))
        text = chr(10).join(lifted)
        self.assertIn('RECOMP_UNIMPL("lock bts', text)
        self.assertNotIn("MEM32(ecx) = ", text)


class XaddFlagsTest(unittest.TestCase):
    """G1 gap 3 (C gate): xadd sets the flags of the add it performs; they used
    to be modelled as undefined and a setcc/jcc after it read a stale _flags."""

    def _block(self, jcc):
        insn = Instruction(0, 5, "xadd", "dword ptr [ecx], edx", "f00fc111", lock=True)
        insn.operands = [_mem_ecx(), Operand(type="reg", reg="edx")]
        j = Instruction(5, 2, jcc, "0x10", "7409")
        j.jump_target = 0x10
        lifter = Lifter()
        lifter.needs_cf = True
        lifted, _ = lift_basic_block(lifter, BasicBlock(start=0, instructions=[insn, j]))
        return chr(10).join(lifted)

    def test_zf_and_cf_after_xadd_come_from_the_add(self):
        jz = self._block("je")
        self.assertNotIn("_flags", jz)
        self.assertIn("_fa = ", jz)
        jc = self._block("jb")
        self.assertNotIn("_flags", jc)
        self.assertIn("_cf = ", jc)
        self.assertIn("RECOMP_ATOMIC_ADD32", jc)
