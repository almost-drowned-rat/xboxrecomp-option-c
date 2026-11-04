"""tools/relift_compare.py: lost functions, lost aliases and new stubs (by address) fail; additions pass."""
import json
import os
import tempfile
import unittest

from tools import relift_compare as rc


def make(d, funcs, stubs):
    os.makedirs(os.path.join(d, "pipeline", "disasm"))
    os.makedirs(os.path.join(d, "gen"))
    with open(os.path.join(d, "pipeline", "disasm", "functions.json"), "w", encoding="utf-8") as f:
        json.dump([{"start": f"0x{a:08X}", "detection_method": m} for a, m in funcs], f)
    with open(os.path.join(d, "gen", "recomp_stubs_unresolved.c"), "w", encoding="utf-8") as f:
        for a in stubs:
            f.write(f"void sub_{a:08X}(RECOMP_PARAMS) {{ (void)base; g_esp += 4; }}\n")


class ReliftCompare(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        self.base = os.path.join(self.t.name, "base")
        make(self.base, [(0x11000, "call_target"), (0x12000, "tail_jump_alias")], [0x16605, 0x16612])

    def tearDown(self):
        self.t.cleanup()

    def head(self, funcs, stubs):
        h = os.path.join(self.t.name, "head")
        make(h, funcs, stubs)
        return h

    def test_identical_passes(self):
        h = self.head([(0x11000, "call_target"), (0x12000, "tail_jump_alias")], [0x16605, 0x16612])
        self.assertTrue(rc.compare(self.base, h)[0])

    def test_additions_and_gone_stubs_pass(self):
        h = self.head([(0x11000, "call_target"), (0x12000, "tail_jump_alias"), (0x13000, "prologue")], [0x16605])
        ok, lines = rc.compare(self.base, h)
        self.assertTrue(ok)
        self.assertIn("  added function 0x00013000", lines)
        self.assertIn("  stub gone 0x00016612", lines)

    def test_lost_alias_fails(self):
        h = self.head([(0x11000, "call_target")], [0x16605, 0x16612])
        ok, lines = rc.compare(self.base, h)
        self.assertFalse(ok)
        self.assertIn("  LOST alias 0x00012000", lines)

    def test_alias_found_by_another_method_passes(self):
        h = self.head([(0x11000, "call_target"), (0x12000, "imm_ref_target")], [0x16605, 0x16612])
        self.assertTrue(rc.compare(self.base, h)[0])

    def test_new_stub_by_address_fails_even_with_equal_count(self):
        h = self.head([(0x11000, "call_target"), (0x12000, "tail_jump_alias")], [0x16605, 0x17000])
        ok, lines = rc.compare(self.base, h)
        self.assertFalse(ok)
        self.assertIn("  NEW stub 0x00017000", lines)

    def test_allowed_loss_passes(self):
        h = self.head([(0x12000, "tail_jump_alias")], [0x16605, 0x16612])
        self.assertFalse(rc.compare(self.base, h)[0])
        self.assertTrue(rc.compare(self.base, h, {0x11000})[0])


if __name__ == "__main__":
    unittest.main()
