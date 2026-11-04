"""A lone code pointer in a code-section gap names a callback (2026-11-01).

Panzer Dragoon Orta's XPP keeps its USB completion callback 0x003046E8 at XPP+0x88, alone between zeros in the
driver's descriptor struct; the callback starts right after the `ret 8` of the function before it. Code-gap pointers
only counted in runs of POINTER_RUN_MIN, so it was never an entry, the first OHCI interrupt hit an unresolved
indirect call and the title ignored START. A lone pointer now gets the same strict entry test as a run's targets:
an instruction boundary in a gap, right after padding or an unconditional ret/jmp, decoding as a body.

Layout (one code section): 0x00 zeros + a pointer at 0x10 + zeros, F1 at 0x20 (`push ebx; xor eax, eax; pop ebx;
ret 8; ret`), F2 at 0x28 right after that `ret`, no padding (`mov eax, ecx; ret`), int3 padding. A lone pointer counts
only for such a start - one after padding is the padding passes' business (test_code_section_tables).
"""
import struct

from tools.disasm.functions import Function, FunctionDetector
from tools.disasm.labels import LabelManager
from tools.disasm.test_returning_body_switch import BASE, _engine
from tools.disasm.xrefs import XRefTracker

F1, F2 = BASE + 0x20, BASE + 0x28


def _detector(pointer):
    data = (b"\x00" * 0x10 + struct.pack("<I", pointer) + b"\x00" * 0x0C
            + bytes.fromhex("5331c05b")             # F1: push ebx; xor eax,eax; pop ebx
            + bytes.fromhex("c20800c3")             # ret 8 (0x24..0x26); 0x27 `ret` - F2 packed right behind it
            + bytes.fromhex("8bc1c3")               # F2 at 0x28: mov eax, ecx; ret
            + b"\xcc" * 16)
    engine, image = _engine(data)
    image.section.raw_size = image.section.virtual_size
    det = FunctionDetector(engine, image, XRefTracker(), LabelManager())
    det.functions.clear()
    det.functions[F1] = Function(start=F1, end=F1 + 8, name=f"sub_{F1:08X}", section=".text",
                                 detection_method="call_target")
    return det, image


def test_a_lone_pointer_to_an_entry_after_padding_is_taken():
    det, image = _detector(F2)
    assert F2 not in det.functions and F2 not in det._alias_entries
    det._pass_data_ptr_targets([image.section])
    assert F2 in det.functions or F2 in det._alias_entries


def test_a_lone_pointer_into_a_body_is_not_a_new_entry_from_the_gap_rule():
    det, image = _detector(F1 + 1)                  # `xor eax, eax` inside F1
    before = set(det._alias_entries)
    det._pass_data_ptr_targets([image.section])
    assert F1 + 1 not in det.functions
    assert set(det._alias_entries) == before or F1 + 1 not in det._alias_entries


def test_a_lone_pointer_to_mid_instruction_is_rejected():
    det, image = _detector(F2 + 1)                  # inside `mov eax, ecx`
    det._pass_data_ptr_targets([image.section])
    assert F2 + 1 not in det.functions and F2 + 1 not in det._alias_entries
