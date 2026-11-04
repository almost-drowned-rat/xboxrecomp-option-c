"""Pointer tables that live inside a code section, between functions.

XDK libraries link their data into their own executable section. Fallout:
Brotherhood of Steel's D3D keeps a 30-entry render-state handler table at
0x000B39F0, between two D3D functions, and three of the handlers are reached
only through it. _pass_data_ptr_targets used to scan data sections only.
(sp00n 7fc6f0d, fce44fb, sp00nz)

option-c: a code-gap word counts only in a run of >= POINTER_RUN_MIN code
pointers, and it names gap code only, with the strict body probe.
"""
import struct

import pytest

from tools.disasm import config
from tools.disasm.engine import DisasmEngine
from tools.disasm.functions import FunctionDetector
from tools.disasm.labels import LabelManager
from tools.disasm.loader import BinaryImage, SectionInfo

BASE = 0x10000
HANDLER = b"\x8b\x44\x24\x04\xc2\x04\x00"   # mov eax, [esp+4] ; ret 4


def _detect(text_bytes, jump_tables=None):
    text = SectionInfo('D3D', BASE, len(text_bytes), 0, len(text_bytes),
                       False, True, '')
    image = BinaryImage('synthetic', text_bytes, 0, 0x20000, BASE, 0, [text])
    engine = DisasmEngine(image)
    engine.linear_sweep(text)
    engine.jump_tables.update(jump_tables or {})
    detector = FunctionDetector(engine, image, None, LabelManager())
    detector._pass_known_addresses()
    detector._build_functions([text])
    detector._pass_data_ptr_targets([text])
    return detector


def _table_layout(entries):
    # +0   ret, padded            (the entry point: a known function)
    # +16  table: `entries` pointers to the handlers at +48, +64, +80
    # +48  handlers nothing calls, 16 bytes apart, int3-padded
    handlers = [BASE + 48 + 16 * i for i in range(3)]
    body = b"\xc3" + b"\xcc" * 15
    table = b"".join(struct.pack("<I", h) for h in handlers[:entries])
    body += table + b"\xcc" * (32 - len(table))
    for _ in handlers:
        body += HANDLER + b"\xcc" * 9
    return body, handlers


def test_table_in_a_code_section_gap_names_its_functions():
    assert config.POINTER_RUN_MIN == 3
    body, handlers = _table_layout(3)
    detector = _detect(body)
    for handler in handlers:
        assert handler in detector._alias_entries


def test_a_short_run_in_a_code_gap_is_not_a_table():
    # option-c: one or two words in a code gap can be any constant.
    body, handlers = _table_layout(2)
    detector = _detect(body)
    for handler in handlers:
        assert handler not in detector._alias_entries


def test_code_gap_table_target_after_data_is_not_a_function():
    # option-c: Burnout 3's XMV section keeps its decoder's VLC tables
    # between functions, in structs with pointers to byte runs. A target
    # that does not follow padding or a ret/jmp is such a run, even when its
    # bytes decode to a body. Here every handler follows `01 01 00 00`.
    body, handlers = _table_layout(3)
    body = bytearray(body)
    for handler in handlers:
        at = handler - BASE
        body[at - 4:at] = b"\x01\x01\x00\x00"
    detector = _detect(bytes(body))
    for handler in handlers:
        assert handler in detector.engine.instructions
        assert handler not in detector._alias_entries


@pytest.mark.parametrize("pad", [
    b"\x8d\x9b\x00\x00\x00\x00",    # lea ebx, [ebx+0]  (MSVC, Burnout 3 0x002406F0)
    b"\x8d\x49\x00",                # lea ecx, [ecx+0]
    b"\x8b\xff",                    # mov edi, edi
    b"\x0f\x1f\x00",                # nop dword ptr [eax]
])
def test_code_gap_table_target_after_wide_padding_is_a_function(pad):
    body, handlers = _table_layout(3)
    body = bytearray(body)
    for handler in handlers:
        at = handler - BASE
        body[at - 8:at] = b"\xc3" + b"\xcc" * (7 - len(pad)) + pad
    detector = _detect(bytes(body))
    for handler in handlers:
        assert handler in detector._alias_entries


def test_word_inside_a_function_body_is_not_a_table():
    # +0  mov eax, <BASE+16> ; ret    -- the immediate looks like a pointer
    # +16 the same handler shape
    body = b"\xb8" + struct.pack("<I", BASE + 16) + b"\xc3" + b"\xcc" * 10
    body += HANDLER
    detector = _detect(body)
    assert BASE + 16 not in detector._alias_entries


def test_switch_table_in_a_gap_is_not_a_table_of_functions():
    # The engine knows +16 is a switch's jump table: its words are case
    # labels, not callables.
    body, handlers = _table_layout(3)
    detector = _detect(body, {BASE + 16: BASE + 28})
    for handler in handlers:
        assert handler not in detector._alias_entries


def test_code_gap_table_names_no_alias_inside_a_function():
    # option-c: the run names +2, an instruction boundary inside the known
    # function at +0 (mov eax, eax; mov eax, eax; ret). A data-section table
    # may name it as an alias; a code-gap table may not.
    body = b"\x8b\xc0\x8b\xc0\xc3" + b"\xcc" * 11
    body += struct.pack("<III", BASE + 2, BASE + 2, BASE + 2) + b"\xcc" * 4
    detector = _detect(body)
    assert BASE + 2 in detector.engine.instructions
    assert BASE + 2 not in detector._alias_entries
