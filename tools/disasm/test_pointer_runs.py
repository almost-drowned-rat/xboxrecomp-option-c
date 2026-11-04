"""T2 step 1: the pointer-run rule's probe (maintainer's safety replacement for the 64-instruction limit).

A data pointer into a gap that fails the 64-instruction probe becomes an alias only when it sits in a run
of >= 3 code pointers AND _probe_pointer_run_target accepts it: the stream reaches ret/jmp/call without
an invalid or data-like opcode and without running into padding or zero-fill, and the address is not a
jump-table slot. Each verdict comes with a reason (pointer_runs.json lists the rejected ones).
"""
from tools.disasm import config
from tools.disasm.functions import FunctionDetector
from tools.disasm.labels import LabelManager
from tools.disasm.test_returning_body_switch import BASE, _engine
from tools.disasm.xrefs import XRefTracker


def _detector(code):
    engine, image = _engine(code + b"\xcc" * 16)
    return FunctionDetector(engine, image, XRefTracker(), LabelManager())


def test_a_long_body_that_reaches_a_ret_is_accepted():
    # 70 x `inc eax` then ret: the 64-instruction probe gives up, the run rule does not
    det = _detector(b"\x40" * 70 + b"\xc3")
    assert not det.engine.probes_as_function_body(BASE, max_insns=64)
    ok, why = det._probe_pointer_run_target(BASE)
    assert ok and "ret" in why, why


def test_a_call_counts_as_a_control_transfer():
    det = _detector(b"\x40" * 3 + b"\xe8\x00\x00\x00\x00" + b"\xc3")
    ok, why = det._probe_pointer_run_target(BASE)
    assert ok and "call" in why, why


def test_data_decoded_as_code_is_rejected():
    # inc eax; in eax, dx; ret -- `in` never appears in compiled game code
    det = _detector(b"\x40\xed\xc3")
    ok, why = det._probe_pointer_run_target(BASE)
    assert not ok and "data-like opcode in" in why, why


def test_running_into_padding_is_rejected():
    det = _detector(b"\x40\x40\xcc\xcc\xc3")
    ok, why = det._probe_pointer_run_target(BASE)
    assert not ok and "padding" in why, why


def test_zero_fill_is_rejected():
    det = _detector(b"\x40\x00\x00\xc3")
    ok, why = det._probe_pointer_run_target(BASE)
    assert not ok and "zero-fill" in why, why


def test_a_jump_table_slot_is_rejected():
    det = _detector(b"\x40" * 8 + b"\xc3")
    det.engine.jump_tables = {BASE: BASE + 8}
    ok, why = det._probe_pointer_run_target(BASE + 4)
    assert not ok and "jump table" in why, why


def test_the_run_threshold_is_three():
    assert config.POINTER_RUN_MIN == 3
