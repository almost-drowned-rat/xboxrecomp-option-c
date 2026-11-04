"""T2 step 2: a callback pushed as an immediate that lands inside a function becomes an alias entry.

    BASE+0:  push BASE+8        68 <cb>      the callback's address taken
    BASE+5:  pop eax            58
    BASE+6:  jmp BASE+10        eb 02
    BASE+8:  cb: inc eax        40           entry after an unconditional jmp
    BASE+9:  ret                c3
    BASE+10: ret                c3

The host body runs over the callback (the jmp skips it), so lifter A's gap-only immediate pass never
saw it. A conditional-branch target named by an immediate is a label of the host and stays one.
"""
import struct

from tools.disasm.functions import FunctionDetector
from tools.disasm.labels import LabelManager
from tools.disasm.test_returning_body_switch import BASE, _engine
from tools.disasm.xrefs import XRefTracker


def _detect(code):
    engine, image = _engine(code + b"\xcc" * 16)
    det = FunctionDetector(engine, image, XRefTracker(), LabelManager())
    det.detect_all([image.section])
    return det


def test_a_pushed_callback_inside_a_function_becomes_an_alias():
    cb = BASE + 8
    code = b"\x68" + struct.pack("<I", cb) + b"\x58\xeb\x02\x40\xc3\xc3"
    det = _detect(code)
    log = {e["target"]: e for e in det.imm_alias_log}
    assert f"0x{cb:08X}" in log, det.imm_alias_log
    assert log[f"0x{cb:08X}"]["accepted"], log
    assert cb in det.functions or cb in det._alias_entries


def test_a_branch_target_named_by_an_immediate_stays_a_label():
    # push BASE+11; pop eax; test eax,eax; jz BASE+11 (label); inc eax; L: ret
    lbl = BASE + 11
    code = b"\x68" + struct.pack("<I", lbl) + b"\x58\x85\xc0\x74\x01\x40\xc3"
    det = _detect(code)
    log = {e["target"]: e for e in det.imm_alias_log}
    entry = log.get(f"0x{lbl:08X}")
    assert entry is None or not entry["accepted"], entry
    assert lbl not in det._alias_entries
