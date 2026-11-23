"""Tests for spin_detect.py: which back edges become RECOMP_SPIN() (step 6c)."""
from tools.recomp import spin_detect


def _loop(body, label="00010000"):
    return [f"loc_{label}: ;"] + ["    " + l for l in body]


def _mark(lines):
    lines = list(lines)
    n = spin_detect.mark_spins(lines)
    return n, lines


BACK = "if (eax != 0) { RECOMP_BACKEDGE(); goto loc_00010000; }"


def test_fixed_poll_is_marked():
    n, lines = _mark(_loop(["eax = MEM32(0x002BB7A0);", "eax &= 1;", BACK]))
    assert n == 1 and "RECOMP_SPIN();" in lines[-1] and "RECOMP_BACKEDGE" not in lines[-1]


def test_poll_through_an_unchanged_register_with_a_timeout_counter():
    n, _ = _mark(_loop(["ecx = MEM32(edx + 0x30);", "esi -= 1;", "_zf = (esi == 0);",
                        "if (_zf) goto loc_00010100;", "if (ecx != eax) { RECOMP_BACKEDGE(); goto loc_00010000; }"]))
    assert n == 1


def test_a_scan_that_moves_its_pointer_is_not():
    n, _ = _mark(_loop(["eax = MEM8(ecx);", "ecx += 1;", BACK]))      # strlen
    assert n == 0
    n, _ = _mark(_loop(["eax = MEM8(ecx);", "ecx++;", BACK]))
    assert n == 0


def test_a_store_or_a_call_is_not():
    assert _mark(_loop(["eax = MEM32(0x1000);", "MEM32(0x1004) = eax;", BACK]))[0] == 0
    assert _mark(_loop(["eax = MEM32(0x1000);", "sub_00020000(ctx, base);", BACK]))[0] == 0
    assert _mark(_loop(["eax = MEM32(0x1000);", "PUSH32(esp, eax);", BACK]))[0] == 0


def test_no_memory_read_or_too_long_is_not():
    assert _mark(_loop(["ecx -= 1;", "if (ecx != 0) { RECOMP_BACKEDGE(); goto loc_00010000; }"]))[0] == 0
    long_body = ["eax = MEM32(0x1000);"] + ["ebx += 1;"] * spin_detect.MAX_LINES + [BACK]
    assert _mark(_loop(long_body))[0] == 0


def test_a_scan_with_a_fixed_comparand_is_not():
    # Orta 0x0001F990: a float search - the moving read MEMF(esi + 4) next to a fixed MEMF(edx + 0xC)
    n, _ = _mark(_loop(["eax = MEM32(esi + 4);", "esi = esi + 4;", "ecx = MEM32(edx + 0xC);",
                        "if (eax != ecx) { RECOMP_BACKEDGE(); goto loc_00010000; }"]))
    assert n == 0


def test_x87_work_is_not():
    n, _ = _mark(_loop(["fp_push(MEMF(0x1000)); /* fld */", "g_fp_cmp = 0;", BACK]))
    assert n == 0


def test_a_label_or_an_unconditional_goto_inside_is_not():
    # Orta 0x000134AD: switch case blocks between the label and a back edge of another path
    assert _mark(_loop(["esi = 0x3596B8;", "goto loc_000134E6;", "", "loc_000134B4: ;",
                        "eax = MEM32(0x342758);", BACK]))[0] == 0
    assert _mark(_loop(["eax = MEM32(0x342758);", "loc_00010004: ;", BACK]))[0] == 0


def test_acquire_after_each_read_and_on_exits_without_backoff():
    lines = _loop(["eax = MEM32(0x002BB7A0);", "if (MEM32(0x1000) == 0) goto loc_00010100;",
                   "eax &= 1;", BACK])
    n = spin_detect.mark_spins(lines, backoff=False)
    assert n == 1
    assert "RECOMP_BACKEDGE();" in lines[-1] and "RECOMP_SPIN" not in "".join(lines)    # no backoff asked
    assert lines[2].strip() == spin_detect.ACQUIRE                                       # after the first read
    assert "{ RECOMP_POLL_ACQUIRE(); goto loc_00010100; }" in lines[3]                    # the exit taken
    assert lines[4].strip() == spin_detect.ACQUIRE                                       # its fall-through
    assert sum(spin_detect.ACQUIRE in l for l in lines) == 3


def test_a_read_in_the_back_edge_condition_gets_the_acquire_after_it():
    lines = _loop(["if (MEM32(0x1000) != 0) { RECOMP_BACKEDGE(); goto loc_00010000; }"])
    assert spin_detect.mark_spins(lines) == 1
    assert "RECOMP_SPIN(); goto loc_00010000;" in lines[1] and "POLL_ACQUIRE" not in lines[1]
    assert lines[2].strip() == spin_detect.ACQUIRE


def test_not_a_poll_gets_nothing():
    lines = _loop(["eax = MEM8(ecx);", "ecx += 1;", BACK])
    before = list(lines)
    assert spin_detect.mark_spins(lines) == 0 and lines == before


def test_a_compound_store_is_a_store():
    assert spin_detect.mem_accesses("MEM32(eax + 4) += 1;") == [("eax + 4", True)]
    assert spin_detect.mem_accesses("if (MEM32(eax) == 0) goto x;") == [("eax", False)]
