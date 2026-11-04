"""C gate, G1 gap 4 (2026-10-24): a shift by cl whose count masks to 0 leaves every
flag alone. Where the shift's flags are read, the lifter saves the previous
setter's flags (_shf) and the masked count (_shk) next to the shift and the
reader selects between the two; shifts by an immediate and shifts whose flags
nobody reads are lifted as before. The values are checked against the CPU by
the conformance case shl_count32_keeps_flags."""
from tools.recomp.disasm import BasicBlock, Disassembler
from tools.recomp.lifter import Lifter, lift_basic_block


def _block(hexcode):
    d = Disassembler()
    insns = [d._decode_instruction(i) for i in d._cs.disasm(bytes.fromhex(hexcode), 0x1000)]
    lifted, state = lift_basic_block(Lifter(), BasicBlock(start=0x1000, instructions=insns))
    return "\n".join(lifted), state


def test_a_read_shift_by_cl_saves_the_previous_flags_and_selects():
    text, _ = _block("85c9 d3e0 0f94c2")          # test ecx, ecx / shl eax, cl / sete dl
    assert "_shf = " in text and "_shk = ((LO8(ecx)) & 31u)" in text
    assert "(_shk ? (_fa == 0) : (_shf & 0x40u))" in text
    assert text.index("_shf = ") < text.index("eax = eax << ")


def test_a_shift_by_an_immediate_is_unchanged():
    text, _ = _block("85c9 c1e004 0f94c2")        # test ecx, ecx / shl eax, 4 / sete dl
    assert "_shk" not in text and "_shf" not in text


def test_a_shift_whose_flags_nobody_reads_is_unchanged():
    text, _ = _block("85c9 d3e0 85c0 0f94c2")     # test / shl eax, cl / test eax, eax / sete
    assert "_shk" not in text and "_shf" not in text


def test_a_read_shift_at_the_end_of_a_block_hands_the_select_on():
    text, state = _block("85c9 d3e8")              # test ecx, ecx / shr eax, cl
    assert "_shk = " in text
    assert state and state[0] == "__shift_sel:shr"


def test_the_saved_word_names_cf_only_where_the_function_declares_it():
    # _cf exists only in functions that read CF (Lifter.needs_cf); the first
    # version named it everywhere and 19 Burnout functions did not compile.
    d = Disassembler()
    insns = [d._decode_instruction(i) for i in d._cs.disasm(bytes.fromhex("01c8 d3e0 0f94c2"), 0x1000)]
    for needs_cf in (False, True):
        lifter = Lifter()
        lifter.needs_cf = needs_cf
        lifted, _ = lift_basic_block(lifter, BasicBlock(start=0x1000, instructions=insns))
        save = next(l for l in lifted if l.startswith("_shf = "))
        assert ("_cf" in save) == needs_cf, save
