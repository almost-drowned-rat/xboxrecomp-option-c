"""Non-x86 sections (Dolby DSP microcode) are not disassembled as x86 and not scanned for code pointers."""
from types import SimpleNamespace

from tools.disasm.loader import BinaryImage, NON_X86_SECTION_NAMES


def _sec(name, executable=True):
    return SimpleNamespace(name=name, executable=executable, raw_size=0x100)


def test_dolby_is_not_a_code_section():
    image = SimpleNamespace(sections=[_sec(".text"), _sec("DSOUND"), _sec("DOLBY"), _sec(".rdata")])
    names = [s.name for s in BinaryImage.get_code_sections(image)]
    assert names == [".text", "DSOUND"], names


def test_the_non_x86_list_names_dolby():
    assert "DOLBY" in NON_X86_SECTION_NAMES
