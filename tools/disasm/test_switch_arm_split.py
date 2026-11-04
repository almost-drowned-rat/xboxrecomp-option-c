"""Switch arms split off their host must still resolve (option C stability fix, 2026-10-18).

Burnout 3 (USA) 0x001DD420 with its table 0x001DD458 and byte-index table 0x001DD470, relocated to the test
image (the bytes are the game's, addresses rebased):

    host+0x00: add eax, -2 ; cmp eax, 0x1c ; ja default
    host+0x08: movzx eax, byte ptr [eax + IDX]
    host+0x0F: jmp dword ptr [eax*4 + TBL]
    host+0x16: arms `mov eax, imm ; ret` x5, host+0x34: default `xor eax, eax ; ret`
    host+0x38: TBL (6 entries), host+0x50: IDX (29 bytes)

In the game the sweep did not register this table, so lifter A ended the host right after the jmp
(0x001DD420-0x001DD436), gap_prologue made arms 2-5 functions of their own, the default arm became a
tail-jump alias, and the first arm (0x001DD436) got nothing: the translator emitted the run-time fallback
instead of a switch, and that index had no target. Out of that context A keeps this switch whole, so the
test sets up exactly the game's boundaries and runs the pass; the first test is the negative control.
"""
import struct

from tools.disasm.functions import Function, FunctionDetector
from tools.disasm.labels import LabelManager
from tools.disasm.test_returning_body_switch import BASE, _engine
from tools.disasm.xrefs import XRefTracker

HOST = BASE + 0x10
TBL, IDX = HOST + 0x38, HOST + 0x50
ARM_OFFS = [0x2E, 0x28, 0x22, 0x16, 0x1C, 0x34]          # table order as in the game
ARMS = [HOST + o for o in ARM_OFFS]
BODY = (bytes.fromhex("83c0fe83f81c772c0fb680") + struct.pack("<I", IDX)
        + bytes.fromhex("ff2485") + struct.pack("<I", TBL)
        + bytes.fromhex("b800050000c3b800060000c3b800020000c3b8000a0000c3b800010000c333c0c390")
        + struct.pack("<6I", *ARMS)
        + bytes.fromhex("0001050203040505050505050505000203050505050505050505010504"))
CODE = b"\xcc" * 0x10 + BODY


def _game_boundaries():
    """The detector state lifter A produced in the game, rebased."""
    engine, image = _engine(CODE + b"\xcc" * 16)
    image.section.raw_size = image.section.virtual_size   # the alias builder reads it
    det = FunctionDetector(engine, image, XRefTracker(), LabelManager())
    det.functions.clear()
    for start, end, method in [(HOST, HOST + 0x16, "call_target"),
                               (HOST + 0x1C, HOST + 0x22, "gap_prologue"),
                               (HOST + 0x22, HOST + 0x28, "gap_prologue"),
                               (HOST + 0x28, HOST + 0x2E, "gap_prologue"),
                               (HOST + 0x2E, HOST + 0x34, "gap_prologue")]:
        det.functions[start] = Function(start=start, end=end, name=f"sub_{start:08X}", section=".text",
                                        detection_method=method)
    det._alias_entries[HOST + 0x34] = HOST + 0x38     # the default arm: a tail-jump alias in the game
    return det


def _resolved(det, a):
    return a in det.functions or a in det._alias_entries


def test_the_game_state_leaves_the_first_arm_without_an_entry():
    det = _game_boundaries()
    assert [hex(a) for a in ARMS if not _resolved(det, a)] == [hex(HOST + 0x16)]


def test_every_arm_of_the_split_two_level_switch_resolves():
    det = _game_boundaries()
    assert det._pass_switch_arm_aliases() == 1
    assert all(_resolved(det, a) for a in ARMS), [hex(a) for a in ARMS if not _resolved(det, a)]
    (entry,) = det.switch_arm_log
    assert entry["table"] == f"0x{TBL:08X}" and entry["arms"] == 6
    assert entry["aliased"] == [f"0x{HOST + 0x16:08X}"] and entry["unresolved"] == []
    det._build_alias_entries()
    assert HOST + 0x16 in det.functions        # materialised as its own body


def test_a_jump_in_a_gap_still_gets_every_arm():
    """2026-11-01: Panzer Dragoon Orta's hand-written Sofdec VLC decoder - the `jmp [reg*4 + table]`
    lies in a gap no function or alias covers when the pass runs (its bodies only become tail-jump aliases in
    the closure passes later). The pass skipped such a site silently; 29 of 166 arms got no entry and the
    run-time fallback found nothing. The gap is the host now: arms from its start up to the table."""
    det = _game_boundaries()
    det.functions.clear()
    det._alias_entries.clear()                       # nothing covers the jump at HOST + 0x0F
    assert det._pass_switch_arm_aliases() == len(set(ARMS))
    assert all(_resolved(det, a) for a in ARMS), [hex(a) for a in ARMS if not _resolved(det, a)]
    (entry,) = det.switch_arm_log
    assert entry["host_is_gap"] is True and entry["arms"] == 6 and entry["unresolved"] == []
    assert entry["host"] == "0x00000000"             # nothing before it in this image: the gap starts at 0


def test_a_table_the_host_keeps_whole_is_left_alone():
    det = _game_boundaries()
    det.functions.clear()
    det._alias_entries.clear()
    det.functions[HOST] = Function(start=HOST, end=TBL, name="sub_host", section=".text",
                                   detection_method="call_target")
    assert det._pass_switch_arm_aliases() == 0 and det.switch_arm_log == []
