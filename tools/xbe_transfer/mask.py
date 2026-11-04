"""Masked signature of a function: its bytes with every operand that names an address left out.

Between two builds a function that did not change keeps its bytes except where it names other code or data that
moved: rel32 call/jmp/jcc targets and absolute addresses (memory displacements and immediates that fall inside the
image). Those operand bytes become wildcards; each one is kept as a *slot* (where, what kind, which address), so a
matched pair of functions also tells where everything it refers to went.

Short (rel8) branches stay in the signature: they jump inside the function, and an unchanged function keeps them.
"""
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import capstone

from .image import Image

_MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
_MD.detail = True
_BRANCH = (capstone.CS_GRP_JUMP, capstone.CS_GRP_CALL)


@dataclass
class Slot:
    offset: int      # from the function start
    size: int
    kind: str        # "rel" (branch target) or "abs" (address operand)
    value: int       # the address it names in its own build


@dataclass
class Signature:
    start: int
    data: bytes                        # the function's bytes
    mask: bytearray                    # 1 = compared, 0 = wildcard
    slots: List[Slot] = field(default_factory=list)
    complete: bool = True              # False: decoding stopped early (data in the range)

    def __len__(self) -> int:
        return len(self.data)

    def anchor(self) -> Tuple[int, bytes]:
        """Longest run of compared bytes: (offset, bytes) - the search key."""
        best, cur = (0, 0), None
        for i, m in enumerate(self.mask):
            if m:
                cur = i if cur is None else cur
                if i + 1 - cur > best[1]:
                    best = (cur, i + 1 - cur)
            else:
                cur = None
        return best[0], self.data[best[0]: best[0] + best[1]]

    def matches(self, other: bytes) -> bool:
        if len(other) < len(self.data):
            return False
        return all(not m or a == b for a, b, m in zip(self.data, other, self.mask))


def signature(img: Image, start: int, size: int) -> Optional[Signature]:
    data = img.read(start, size)
    if len(data) < size or size <= 0:
        return None
    sig = Signature(start, data, bytearray(b"\x01" * size))
    pos = 0
    for ins in _MD.disasm(data, start):
        pos = ins.address - start + ins.size
        off = ins.address - start
        is_branch = any(g in ins.groups for g in _BRANCH)
        if ins.imm_size == 4:
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_IMM:
                    v = op.imm & 0xFFFFFFFF
                    if is_branch:
                        _add(sig, off + ins.imm_offset, 4, "rel", v)
                    elif img.contains(v):
                        _add(sig, off + ins.imm_offset, 4, "abs", v)
                    break
        if ins.disp_size == 4:
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_MEM:
                    v = op.mem.disp & 0xFFFFFFFF
                    if img.contains(v):
                        _add(sig, off + ins.disp_offset, 4, "abs", v)
                    break
    if pos < size:
        sig.complete = False
    return sig


def _add(sig: Signature, offset: int, size: int, kind: str, value: int) -> None:
    for i in range(offset, offset + size):
        sig.mask[i] = 0
    sig.slots.append(Slot(offset, size, kind, value))


def slot_values(sig: Signature, img: Image, start: int) -> List[Tuple[Slot, int]]:
    """The addresses the same slots name in another build, for the copy of `sig` at `start`."""
    out = []
    for s in sig.slots:
        raw = int.from_bytes(img.read(start + s.offset, 4), "little")
        if s.kind == "rel":
            # rel32 counts from the end of the instruction, and in call/jmp/jcc rel32 the displacement is the
            # instruction's last 4 bytes, so the end is slot offset + 4.
            raw = (start + s.offset + 4 + raw) & 0xFFFFFFFF
        out.append((s, raw))
    return out
