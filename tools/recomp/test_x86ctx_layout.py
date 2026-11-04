"""The guest register set (X86Ctx, option C) is defined twice: in the generated
code's header (templates/runtime/recomp_types.h, copied next to every lift) and
in the runtime's (src/kernel/xbox_memory_layout.h). Generated code and runtime
have to agree on its layout, and the compiler cannot check that across the two
headers -- so this does: the guarded blocks must be identical, byte for byte.
"""
import re
import unittest
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[2]
_HEADERS = (_ROOT / "templates" / "runtime" / "recomp_types.h",
            _ROOT / "src" / "kernel" / "xbox_memory_layout.h")


def _block(path):
    text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    m = re.search(r"^#ifndef RECOMP_X86CTX_DEFINED\n.*?^#endif\n", text,
                  re.M | re.S)
    if not m:
        raise AssertionError(f"{path.name}: no RECOMP_X86CTX_DEFINED block")
    return m.group(0)


class X86CtxLayoutTest(unittest.TestCase):
    def test_both_headers_define_the_same_register_set(self):
        a, b = (_block(p) for p in _HEADERS)
        self.assertEqual(a, b, "X86Ctx differs between recomp_types.h and "
                               "xbox_memory_layout.h")

    def test_gpr_fields_cannot_collide_with_register_macros(self):
        # Generated code has `#define esp g_esp`; a member named esp would be
        # rewritten inside g_esp's own expansion -> (g_x86.g_esp).
        block = _block(_HEADERS[0])
        struct = block[block.index("typedef struct RECOMP_CTX_ALIGN X86Ctx"):block.index("} X86Ctx;")]
        for reg in ("eax", "ecx", "edx", "ebx", "esp", "esi", "edi"):
            self.assertNotRegex(struct, rf"[\s,]{reg}[,;]")


    def test_register_set_is_64_byte_aligned_in_both_headers(self):
        # docs/option_c.md (F5): hot guest code is ~35 % slower when the set sits
        # at an odd multiple of 16 bytes; the type fixes the alignment everywhere.
        for path in _HEADERS:
            block = _block(path)
            self.assertIn("typedef struct RECOMP_CTX_ALIGN X86Ctx", block, path)
            self.assertIn("RECOMP_CTX_ALIGNOF(X86Ctx) == 64", block, path)

if __name__ == "__main__":
    unittest.main()
