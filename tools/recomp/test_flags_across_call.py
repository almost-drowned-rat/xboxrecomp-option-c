"""Flags live across call/ret (lifter CALL_FLAGS): which calls count as flag-live, and how the caller reads the flags
the callee handed over. The runtime behaviour is the conformance corpus case c_flags_across_call."""
from types import SimpleNamespace

from tools.recomp.lifter import CALL_FLAGS, _flags_word, _make_condition
from tools.recomp.translator import _flag_live_calls


def insn(addr, size, mnemonic, call_target=None, cond=False, branch=False, ret=False):
    return SimpleNamespace(address=addr, end_address=addr + size, mnemonic=mnemonic, call_target=call_target,
                           is_call=mnemonic == "call", is_cond_jump=cond, is_branch=branch or cond, is_ret=ret)


def test_jcc_after_call_with_only_preserving_insns_between_is_flag_live():
    code = [insn(0x100, 5, "call", 0x900), insn(0x105, 2, "fld"), insn(0x107, 2, "mov"), insn(0x109, 2, "je", cond=True)]
    assert _flag_live_calls(code) == {0x100: 0x900}


def test_a_setter_or_a_gap_or_an_indirect_call_is_not():
    setter = [insn(0x100, 5, "call", 0x900), insn(0x105, 2, "test"), insn(0x107, 2, "je", cond=True)]
    gap = [insn(0x100, 5, "call", 0x900), insn(0x110, 2, "je", cond=True)]
    indirect = [insn(0x100, 2, "call", None), insn(0x102, 2, "je", cond=True)]
    fcomi = [insn(0x100, 5, "call", 0x900), insn(0x105, 2, "fcomip"), insn(0x107, 2, "jbe", cond=True)]
    for code in (setter, gap, indirect, fcomi):
        assert _flag_live_calls(code) == {}


def test_setcc_and_sbb_read_flags_too():
    for m in ("setne", "sbb", "cmovb"):
        assert _flag_live_calls([insn(0x100, 5, "call", 0x900), insn(0x105, 3, m)]) == {0x100: 0x900}


def test_conditions_read_g_call_flags_in_pushfd_layout():
    cond = lambda j: _make_condition(j, CALL_FLAGS, [])[0]
    assert "(g_call_flags & 0x40u)" in cond("je") and cond("jne").startswith("!(g_call_flags & 0x40u)")
    assert "(g_call_flags & 0x41u)" in cond("jbe") and "0x1u" in cond("jb")
    assert ">> 7 ^ g_call_flags >> 11" in cond("jl") and "0x40u" in cond("jg")
    # the callee's side: every flag a jcc can ask for is in the word
    word = _flags_word(CALL_FLAGS, [])
    for bit in ("0x800u", "0x80u", "0x40u", "0x4u", "0x1u"):
        assert bit in word
