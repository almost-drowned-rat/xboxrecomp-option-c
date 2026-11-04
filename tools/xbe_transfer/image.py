"""The loaded image of an XBE: sections at their virtual addresses (only what matching needs)."""
from dataclasses import dataclass, field
from typing import List, Optional


@dataclass
class Section:
    name: str
    va: int
    data: bytes
    executable: bool

    @property
    def end(self) -> int:
        return self.va + len(self.data)


@dataclass
class Image:
    sections: List[Section] = field(default_factory=list)

    @property
    def lo(self) -> int:
        return min(s.va for s in self.sections)

    @property
    def hi(self) -> int:
        return max(s.end for s in self.sections)

    def section_at(self, va: int) -> Optional[Section]:
        for s in self.sections:
            if s.va <= va < s.end:
                return s
        return None

    def section(self, name: str) -> Optional[Section]:
        for s in self.sections:
            if s.name == name:
                return s
        return None

    def read(self, va: int, size: int) -> bytes:
        s = self.section_at(va)
        if s is None:
            return b""
        return s.data[va - s.va: va - s.va + size]

    def contains(self, va: int) -> bool:
        return self.lo <= va < self.hi


def from_xbe(path: str) -> Image:
    """Sections as loaded (raw bytes, zero-filled up to the virtual size)."""
    from tools.xbe_parser.xbe_parser import SECTION_EXECUTABLE, XBEParser
    xbe = XBEParser(path).parse()
    raw = open(path, "rb").read()
    img = Image()
    for s in xbe.sections:
        data = raw[s.raw_addr: s.raw_addr + s.raw_size]
        if len(data) < s.virtual_size:
            data += bytes(s.virtual_size - len(data))
        img.sections.append(Section(s.name, s.virtual_addr, data[: s.virtual_size], bool(s.flags & SECTION_EXECUTABLE)))
    return img
