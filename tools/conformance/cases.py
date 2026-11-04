"""Snippets whose net effect lands in eax.

Each case is a short x86 sequence, written as MSVC inline-assembly text so the
assembler -- not us -- decides the encoding. The same bytes are then executed
natively and lifted to C, and the two eax values are compared.

Inputs are (eax, ecx) pairs, chosen for the edges that matter: sign-bit
boundaries at each operand width, zero, and -1.
"""

def Case(name, why, asm, inputs, kind="gpr", tol=0.0):
    """One snippet, its inputs, and what gets compared afterwards.

    kind:
      "gpr"  inputs are (eax, ecx) uint32 pairs; eax is compared.
      "fpu"  inputs are (double, double) written to the scratch buffer; the
             x87 stack *depth* and every live st(i) are compared, plus eax
             (so `fnstsw ax` cases work). Depth is the point: a handler that
             pops the wrong number of times is the bug class that desynced
             Halo's camera maths, and the value alone will not show it.
      "sse"  inputs are two 4-float tuples in the scratch buffer; all eight
             XMM registers are compared as raw bytes, plus eax.

    For "fpu" and "sse", eax points at a 64-byte scratch buffer:
      [eax]      operand A (16 bytes: 4 floats, or a double in the low 8)
      [eax+16]   operand B
      [eax+32]   free space for the snippet to store into
    Guest addresses equal host addresses in the harness, so both sides read
    the same bytes.

    tol: relative tolerance for "fpu" comparisons. Zero -- bit-exact -- is the
    default and is what almost everything should use: at PC=53 the hardware
    rounds add/sub/mul/div/sqrt to double exactly as C does. Only the
    transcendentals need slack, because the x87's polynomial and libm's are
    different implementations of the same function and differ in the last
    place. That is a real divergence between the recompiled code and the
    hardware, not a lifting bug, and it is recorded here rather than hidden by
    a blanket tolerance so a genuinely wrong result still fails.
    """
    return {"name": name, "why": why, "asm": asm, "inputs": inputs,
            "kind": kind, "tol": tol}

# Values that sit on a boundary at one width but not another. A bug that
# evaluates an 8- or 16-bit operand at 32 bits shows up here and nowhere else.
_EDGES = [0x00000000, 0x00000001, 0x0000007F, 0x00000080, 0x000000FF,
          0x00007FFF, 0x00008000, 0x0000FFFF, 0x7FFFFFFF, 0x80000000,
          0xFFFFFFFF, 0x12345678]

_PAIRS = [(a, b) for a in _EDGES for b in (0x00000001, 0x0000007F, 0x00000080,
                                           0x000000FF, 0xFFFFFFFF)]

# Shift counts in cl: below, at and above each operand width, and >= 32 where
# only the 5-bit mask keeps the result defined.
_SHIFT_COUNTS = [(a, c) for a in _EDGES for c in (7, 8, 9, 15, 16, 17, 31, 32, 33, 63)]

# Doubles for the x87 cases. Ordinary values, the signed zeroes, a tie for
# the rounding modes, and one pair that is only interesting in reverse
# (fsubr/fdivr swap the operands, so a/b and b/a must differ).
_FP = [
    (1.0, 2.0), (2.0, 1.0), (-3.25, 0.5), (0.5, -3.25),
    (100.0, 7.0), (7.0, 100.0), (1.0, 3.0), (3.0, 1.0),
    (0.0, 1.0), (1.0, 0.0), (-0.0, 1.0), (1.0, -0.0),
    (2.5, 2.0), (-2.5, 2.0), (0.5, 0.5), (-1.0, -1.0),
    (1e10, 3.0), (1e-10, 3.0), (65536.0, 256.0),
]

# Only for the cases that mask fnstsw down to the condition codes. The
# status word's low byte carries the exception flags -- invalid is set by
# a NaN compare -- and those are not modelled, so a case comparing the
# whole of ax would diverge on bits that have nothing to do with the
# comparison it is testing.
_FP_NAN = [
    (float('nan'), 1.0), (1.0, float('nan')),
    (float('nan'), float('nan')), (1.0, 2.0), (2.0, 1.0),
    (1.0, 1.0), (float('inf'), 1.0), (-0.0, 0.0),
]

# Four-float lanes for the SSE cases. Lane 0 usually differs from lanes 1-3 so
# a scalar-only lift shows up as three wrong lanes rather than a wrong answer,
# and the last two rows carry NaN and the signed zeroes for the compare and
# min/max tie-break cases.
_NAN = float("nan")
_SSE = [
    ((1.0, 2.0, 3.0, 4.0),        (10.0, 20.0, 30.0, 40.0)),
    ((-1.0, -2.0, -3.0, -4.0),    (1.0, 0.5, 0.25, 0.125)),
    ((0.0, 1.0, -1.0, 0.5),       (1.0, 1.0, 1.0, 1.0)),
    ((3.0, 3.0, 3.0, 3.0),        (3.0, 3.0, 3.0, 3.0)),
    ((1.5, -2.5, 1e10, 1e-10),    (2.5, -1.5, 1e-10, 1e10)),
    ((_NAN, 1.0, -0.0, 0.0),      (1.0, _NAN, 0.0, -0.0)),
    ((-0.0, 0.0, _NAN, _NAN),     (0.0, -0.0, _NAN, 1.0)),
]

CASES = [
    # -- comisd/ucomisd read the low double of a register (sp00n bbbaadb) ----
    Case("comisd_register_lane", "double register compare uses the 64-bit lane",
         ["movsd xmm0, qword ptr [eax]", "movsd xmm1, qword ptr [eax+16]",
          "comisd xmm0, xmm1", "setb al", "movzx eax, al"], _FP_NAN, kind="fpu"),
    Case("ucomisd_register_lane", "double register compare uses the 64-bit lane",
         ["movsd xmm0, qword ptr [eax]", "movsd xmm1, qword ptr [eax+16]",
          "ucomisd xmm0, xmm1", "setb al", "movzx eax, al"], _FP_NAN, kind="fpu"),

    # -- parity SETcc/CMOVcc after cmp/test (sp00n 2db0a2f) ------------------
    Case("cmp_setp", "parity reader after a known flag producer",
         ['cmp eax, ecx', 'setp al', 'movzx eax, al'], _PAIRS),
    Case("cmp_setnp", "parity reader after a known flag producer",
         ['cmp eax, ecx', 'setnp al', 'movzx eax, al'], _PAIRS),
    Case("cmp_cmovp", "parity reader after a known flag producer",
         ['cmp eax, ecx', 'cmovp eax, ecx'], _PAIRS),
    Case("cmp_cmovnp", "parity reader after a known flag producer",
         ['cmp eax, ecx', 'cmovnp eax, ecx'], _PAIRS),
    Case("test_setp", "parity reader after a known flag producer",
         ['test eax, ecx', 'setp al', 'movzx eax, al'], _PAIRS),
    Case("test_setnp", "parity reader after a known flag producer",
         ['test eax, ecx', 'setnp al', 'movzx eax, al'], _PAIRS),
    Case("test_cmovp", "parity reader after a known flag producer",
         ['test eax, ecx', 'cmovp eax, ecx'], _PAIRS),
    Case("test_cmovnp", "parity reader after a known flag producer",
         ['test eax, ecx', 'cmovnp eax, ecx'], _PAIRS),

    # -- signed compare width (the RECOMP_SIGNED vs SXV decision) -------------
    Case("setl_i8", "cmp at byte width, then jl's condition",
         ["cmp al, cl", "setl al", "movzx eax, al"], _PAIRS),
    Case("setl_i16", "cmp at word width",
         ["cmp ax, cx", "setl al", "movzx eax, al"], _PAIRS),
    Case("setl_i32", "cmp at dword width",
         ["cmp eax, ecx", "setl al", "movzx eax, al"], _PAIRS),
    Case("setle_i8", "<= at byte width",
         ["cmp al, cl", "setle al", "movzx eax, al"], _PAIRS),
    Case("setg_i16", "> at word width",
         ["cmp ax, cx", "setg al", "movzx eax, al"], _PAIRS),
    Case("setb_i32", "unsigned below, for contrast with the signed forms",
         ["cmp eax, ecx", "setb al", "movzx eax, al"], _PAIRS),

    # -- test / sign flag ----------------------------------------------------
    Case("tests_i8", "SF from an 8-bit test",
         ["test al, cl", "sets al", "movzx eax, al"], _PAIRS),
    Case("testz_i16", "ZF from a 16-bit test",
         ["test ax, cx", "setz al", "movzx eax, al"], _PAIRS),

    # -- xor zeroing must clear an incoming carry ----------------------------
    Case("xor_self_adc_i8", "byte zeroing clears CF before adc",
         ["stc", "xor cl, cl", "adc eax, 0"], [(a, 0) for a in _EDGES]),
    Case("xor_self_adc_hi8", "high-byte zeroing clears CF before adc",
         ["stc", "xor ch, ch", "adc eax, 0"], [(a, 0) for a in _EDGES]),
    Case("xor_self_adc_i16", "word zeroing clears CF before adc",
         ["stc", "xor cx, cx", "adc eax, 0"], [(a, 0) for a in _EDGES]),
    Case("xor_self_adc_i32", "dword zeroing clears CF before adc",
         ["stc", "xor ecx, ecx", "adc eax, 0"], [(a, 0) for a in _EDGES]),
    Case("xor_self_sbb_i32", "dword zeroing clears CF before sbb",
         ["stc", "xor ecx, ecx", "sbb eax, 0"], [(a, 0) for a in _EDGES]),

    # -- neg carry into a dependent sbb (PR #8) ------------------------------
    Case("neg_sbb_adjacent", "neg sets CF = (operand != 0); sbb consumes it",
         ["neg eax", "sbb ecx, ecx", "mov eax, ecx"], _PAIRS),
    Case("neg_sbb_separated",
         "same, with flag-safe instructions in between -- the real codegen shape",
         ["neg eax", "push edx", "mov edx, 1", "pop edx",
          "sbb ecx, ecx", "mov eax, ecx"], _PAIRS),
    Case("neg_adc", "neg's carry into adc",
         ["neg eax", "adc ecx, 0", "mov eax, ecx"], _PAIRS),

    # -- rotate through carry ------------------------------------------------
    Case("rcr_divide_normaliser", "shr's carry into rcr, as MSVC's 64-bit divide",
         ["shr ecx, 1", "rcr eax, 1"], _PAIRS),
    Case("rcl_i32_cl", "rcl by a variable count",
         ["stc", "rcl eax, cl"], _PAIRS),
    Case("rcr_i8_cl_setc", "byte rcr, count mod 9, carry out read by setc",
         ["stc", "rcr al, cl", "setc ah"], _PAIRS),
    Case("rcl_i16_cl_setc", "word rcl, count mod 17, carry out read by setc",
         ["clc", "rcl ax, cl", "setc cl", "movzx ecx, cl",
          "shl ecx, 16", "movzx eax, ax", "or eax, ecx"], _PAIRS),

    # -- sign/zero extension -------------------------------------------------
    Case("movsx_8_32", "sign-extend byte to dword",
         ["movsx eax, al"], _PAIRS),
    Case("movsx_16_32", "sign-extend word to dword",
         ["movsx eax, ax"], _PAIRS),
    Case("movsx_bp", "sign-extend bp: the 16-bit list once stopped at si/di "
         "(Bink's IDCT in THPS 2X, movsx ebp, bp)",
         ["push ebp", "mov ebp, eax", "movsx ebp, bp", "mov eax, ebp", "pop ebp"],
         _PAIRS),
    Case("movsx_8_32_from_partial_load", "mov bp, word ptr then movsx, as the IDCT does",
         ["push ebp", "push eax", "mov ebp, ecx", "mov bp, word ptr [esp]",
          "movsx ebp, bp", "mov eax, ebp", "add esp, 4", "pop ebp"], _PAIRS),
    Case("movzx_8_32", "zero-extend byte to dword",
         ["movzx eax, al"], _PAIRS),
    # Divisor forced odd and positive: idiv raises #DE both on a zero divisor
    # and on INT32_MIN / -1, and a trapping case tells us nothing about lifting.
    Case("cdq_idiv", "cdq's sign-extend feeding a signed divide",
         ["or ecx, 1", "and ecx, 07FFFFFFFh", "cdq", "idiv ecx"], _PAIRS),

    # -- shifts --------------------------------------------------------------
    Case("shl_cl", "variable shift left, including the &31 masking",
         ["shl eax, cl"], _PAIRS),
    Case("sar_cl", "arithmetic right shift keeps the sign",
         ["sar eax, cl"], _PAIRS),
    Case("shr_cl", "logical right shift does not",
         ["shr eax, cl"], _PAIRS),
    Case("rol_cl", "rotate left",
         ["rol eax, cl"], _PAIRS),
    # x86 masks the count to 5 bits at every width; in C a shift of 32 or more
    # is undefined (THPS replay bit reader: c1ac219).
    Case("shl_cl_wide", "shl with counts >= 32 (count & 31)",
         ["shl eax, cl"], _SHIFT_COUNTS),
    Case("shr_cl_wide", "shr with counts >= 32 (count & 31)",
         ["shr eax, cl"], _SHIFT_COUNTS),
    Case("shl_cl_8", "8-bit shl, counts 8..31 clear the byte",
         ["shl al, cl", "movzx eax, al"], _SHIFT_COUNTS),
    Case("shr_cl_8", "8-bit shr, counts 8..31 clear the byte",
         ["shr al, cl", "movzx eax, al"], _SHIFT_COUNTS),
    Case("shl_cl_16", "16-bit shl, counts 16..31 clear the word",
         ["shl ax, cl", "movzx eax, ax"], _SHIFT_COUNTS),
    Case("shr_cl_16", "16-bit shr, counts 16..31 clear the word",
         ["shr ax, cl", "movzx eax, ax"], _SHIFT_COUNTS),
    Case("shld", "double-precision shift",
         ["shld eax, ecx, 5"], _PAIRS),

    # -- arithmetic ----------------------------------------------------------
    Case("imul_32", "signed multiply, low half",
         ["imul eax, ecx"], _PAIRS),
    Case("add_adc_64", "the 64-bit add idiom: add produces carry, adc consumes",
         ["add eax, ecx", "adc edx, 0", "mov eax, edx"], _PAIRS),
    Case("sub_sbb_64", "the 64-bit subtract idiom",
         ["sub eax, ecx", "sbb edx, 0", "mov eax, edx"], _PAIRS),
    Case("inc_dec", "inc/dec leave CF alone -- a classic place to get flags wrong",
         ["stc", "inc eax", "adc ecx, 0", "mov eax, ecx"], _PAIRS),
    Case("bswap", "byte swap", ["bswap eax"], _PAIRS),


    # The dword string compares were emitted as a bare comment until the
    # Wreckless bring-up: nothing compared, esi/edi never advanced, and the
    # jcc reading the flags went wherever the previous instruction left them.
    # Buffers come off the stack so the case needs no external symbol.
    Case("repe_cmpsd_equal", "repe cmpsd over identical dwords sets ZF",
         ["push 0x11111111", "push 0x11111111",
          "mov esi, esp", "mov edi, esp", "mov ecx, 2", "repe cmpsd",
          "setz al", "movzx eax, al", "add esp, 8"], _PAIRS),
    Case("repe_cmpsd_differ", "and clears it when they differ",
         ["push 0x22222222", "push 0x11111111",
          "mov esi, esp", "lea edi, [esp+4]", "mov ecx, 1", "repe cmpsd",
          "setz al", "movzx eax, al", "add esp, 8"], _PAIRS),
    Case("repne_scasd", "repne scasd stops on a match and leaves edi past it",
         ["push 0x44444444", "push 0x33333333",
          "mov edi, esp", "mov eax, 0x44444444", "mov ecx, 2", "repne scasd",
          "sub edi, esp", "mov eax, edi", "add esp, 8"], _PAIRS),

    # bsf/bsr were emitted as TODO comments until the Wreckless bring-up.
    # RtlAllocateHeap picks a free-list bucket by bsf-ing an in-use bitmap, so
    # a bsf that does nothing returns its own argument and the allocator hands
    # out the address of an empty list head. Both the value and the zero case
    # (destination untouched, ZF set) are checked.
    Case("bsf", "bit scan forward: index of the lowest set bit",
         ["bsf eax, ecx"], _PAIRS),
    Case("bsr", "bit scan reverse: index of the highest set bit",
         ["bsr eax, ecx"], _PAIRS),
    Case("bsf_zero_leaves_dest", "bsf with a zero source must not write dest",
         ["xor ecx, ecx", "bsf eax, ecx"], _PAIRS),
    Case("bsf_zf", "ZF after bsf is set exactly when the source was zero",
         ["bsf eax, ecx", "setz al", "movzx eax, al"], _PAIRS),
    Case("bsr_zf", "and the same for bsr",
         ["bsr eax, ecx", "setz al", "movzx eax, al"], _PAIRS),

    # Nonzero-forced variants and a scan into a different destination,
    # contributed in #16. The cases above leave the source to the input
    # pairs, so these pin the common path explicitly.
    Case("bsf_nonzero", "least-significant set bit, with a nonzero source",
         ["or ecx, 1", "bsf eax, ecx"], _PAIRS),
    Case("bsr_nonzero", "most-significant set bit, with a nonzero source",
         ["or ecx, 1", "bsr eax, ecx"], _PAIRS),
    Case("bit_scan_zf", "bit scan sets ZF when its source is zero",
         ["bsf edx, ecx", "setz al", "movzx eax, al"], _PAIRS),
    Case("not_and", "bitwise", ["not eax", "and eax, ecx"], _PAIRS),


    # ══ MMX ══════════════════════════════
    #
    # Every one of these was a TODO comment, which is a silent no-op: the
    # destination kept its previous value. Wreckless's WMV decoder is 1796
    # MMX instructions of IDCT and motion compensation and D3DX's texture
    # converters another 900, so both wrote whatever was already there.
    #
    # The harness uses a 32-bit MSVC, so the host executes real MMX and is
    # a true reference. Each case builds mm0/mm1 from the vector pair and
    # reports one dword of the result.
    Case("mmx_paddw", "packed word add wraps per lane",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'paddw mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_paddd", "packed dword add",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'paddd mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_psubw", "packed word subtract",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'psubw mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_paddsw", "saturating signed word add",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'paddsw mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_pmullw", "packed word multiply, low half",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'pmullw mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_pmulhw", "packed word multiply, high half",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'pmulhw mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_pmaddwd", "multiply words and add adjacent pairs",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'pmaddwd mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_psraw", "arithmetic word shift right saturates its count",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'psraw mm0, 3', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_psrlw", "logical word shift right",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'psrlw mm0, 5', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_pslld", "dword shift left",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'pslld mm0, 7', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_psrlq", "whole-register shift right",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'psrlq mm0, 11', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_psraw_big", "a count past the lane width is a full sign fill",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'psraw mm0, 40', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_psrlw_big", "and zero for the logical form",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'psrlw mm0, 40', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_punpcklbw", "interleave low bytes",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'punpcklbw mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_punpckhbw", "interleave high bytes",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'punpckhbw mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_punpcklwd", "interleave low words",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'punpcklwd mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_punpckhdq", "interleave high dwords",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'punpckhdq mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_packuswb", "pack words to unsigned bytes with saturation",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'packuswb mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_packssdw", "pack dwords to signed words with saturation",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'packssdw mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_pcmpgtw", "packed signed word compare",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'pcmpgtw mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_pand", "bitwise and",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'pand mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_pandn", "and-not is ~dst & src, not dst & ~src",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'pandn mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_pxor", "bitwise xor",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'pxor mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_pavgb", "unsigned byte average rounds up",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'pavgb mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_pshufw", "word shuffle by immediate",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'pshufw mm0, mm1, 0x1B', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+0]', 'add esp, 8', 'emms'], _PAIRS),
    Case("mmx_paddw_hi", "the high lane too, not just lane 0",
         ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]', 'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32', 'paddw mm0, mm1', 'push 0', 'push 0', 'movq qword ptr [esp], mm0', 'mov eax, dword ptr [esp+4]', 'add esp, 8', 'emms'], _PAIRS),

    # ══ x87 ═════════════════════════════════════════════════════════════════
    #
    # Stack depth is compared as well as the values. Most of the x87 bugs this
    # project has had were pop-count bugs, not arithmetic bugs: a handler that
    # forgets to pop leaks a slot, everything after it reads st(i) one off, and
    # the *first* value still looks right.

    Case("fpu_load_store", "fld then fstp round-trips and leaves the stack empty",
         ["fld qword ptr [eax]", "fstp qword ptr [eax+32]"], _FP, "fpu"),
    Case("fpu_fst_no_pop", "fst stores WITHOUT popping; only fstp pops",
         ["fld qword ptr [eax]", "fst qword ptr [eax+32]"], _FP, "fpu"),
    Case("fpu_fld_st0_dup", "fld st(0) pushes a copy -- depth 2, not 1",
         ["fld qword ptr [eax]", "fld st(0)"], _FP, "fpu"),
    Case("fpu_add_mem", "fadd [mem] updates st0 in place and does not pop",
         ["fld qword ptr [eax]", "fadd qword ptr [eax+16]"], _FP, "fpu"),
    Case("fpu_addp", "faddp adds into st1 and pops",
         ["fld qword ptr [eax]", "fld qword ptr [eax+16]", "faddp st(1), st(0)"],
         _FP, "fpu"),
    Case("fpu_sub_mem", "fsub [mem]: st0 = st0 - mem",
         ["fld qword ptr [eax]", "fsub qword ptr [eax+16]"], _FP, "fpu"),
    Case("fpu_subr_mem", "fsubr [mem]: st0 = mem - st0, the reversed form",
         ["fld qword ptr [eax]", "fsubr qword ptr [eax+16]"], _FP, "fpu"),
    Case("fpu_div_mem", "fdiv [mem]: st0 = st0 / mem",
         ["fld qword ptr [eax]", "fdiv qword ptr [eax+16]"], _FP, "fpu"),
    Case("fpu_divr_mem",
         "fdivr [mem]: st0 = mem / st0 -- dropping the reverse turns a "
         "normalise into its own inverse",
         ["fld qword ptr [eax]", "fdivr qword ptr [eax+16]"], _FP, "fpu"),
    Case("fpu_mul_sti", "fmul st(1): st0 *= st1, no pop",
         ["fld qword ptr [eax]", "fld qword ptr [eax+16]", "fmul st(0), st(1)"],
         _FP, "fpu"),
    Case("fpu_mulp_sti", "fmulp st(1): st1 *= st0, then pop",
         ["fld qword ptr [eax]", "fld qword ptr [eax+16]", "fmulp st(1), st(0)"],
         _FP, "fpu"),
    Case("fpu_xch", "fxch swaps st0 and st1",
         ["fld qword ptr [eax]", "fld qword ptr [eax+16]", "fxch st(1)"],
         _FP, "fpu"),
    Case("fpu_chs_abs", "fchs / fabs",
         ["fld qword ptr [eax]", "fchs", "fabs", "fchs"], _FP, "fpu"),
    Case("fpu_sqrt", "fsqrt", ["fld qword ptr [eax]", "fabs", "fsqrt"],
         _FP, "fpu"),
    Case("fpu_cmp_pops",
         "fcompp compares and pops TWICE; fnstsw ax then carries the result "
         "into eax -- the whole pre-SSE float branch idiom in one case",
         ["fld qword ptr [eax+16]", "fld qword ptr [eax]", "fcompp",
          "fnstsw ax"], _FP, "fpu"),
    Case("fpu_comp_one_pop", "fcomp pops once, fcom not at all",
         ["fld qword ptr [eax+16]", "fld qword ptr [eax]",
          "fcom st(1)", "fcomp st(1)", "fnstsw ax"], _FP, "fpu"),
    Case("fpu_int_roundtrip", "fild / fistp integer conversion",
         ["fild dword ptr [eax]", "fistp dword ptr [eax+32]",
          "fild dword ptr [eax+32]"], _FP, "fpu"),
    Case("fpu_rndint", "frndint rounds per the control word",
         ["fld qword ptr [eax]", "frndint"], _FP, "fpu"),
    Case("fpu_patan", "fpatan: st1 = atan2(st1, st0), pop",
         ["fld qword ptr [eax]", "fld qword ptr [eax+16]", "fpatan"],
         _FP, "fpu", tol=1e-15),
    Case("fpu_sincos_pair", "fsin and fcos",
         ["fld qword ptr [eax]", "fsin", "fld qword ptr [eax]", "fcos"],
         _FP, "fpu", tol=1e-15),
    Case("fpu_ptan", "fptan replaces st0 and pushes 1.0 -- depth grows by one",
         ["fld qword ptr [eax]", "fptan"], _FP, "fpu", tol=1e-15),

    # ══ SSE ═════════════════════════════════════════════════════════════════
    #
    # All four lanes are compared, which is the whole point: modelling XMM as a
    # scalar float made movaps move 4 of 16 bytes and nothing noticed.

    Case("sse_movaps", "movaps moves all 16 bytes, not just lane 0",
         ["movaps xmm0, xmmword ptr [eax]",
          "movaps xmmword ptr [eax+32], xmm0"], _SSE, "sse"),
    Case("sse_movups", "unaligned move, same 16 bytes",
         ["movups xmm1, xmmword ptr [eax+16]"], _SSE, "sse"),
    Case("sse_arith", "packed add/sub/mul/div are lane-wise",
         ["movaps xmm0, xmmword ptr [eax]",
          "movaps xmm1, xmmword ptr [eax+16]",
          "addps xmm0, xmm1", "subps xmm0, xmm1",
          "mulps xmm0, xmm1", "divps xmm0, xmm1"], _SSE, "sse"),
    Case("sse_minmax",
         "MINPS/MAXPS return the SECOND operand on a tie or unordered "
         "compare -- that is the hardware tie-break, not fmin/fmax",
         ["movaps xmm0, xmmword ptr [eax]",
          "movaps xmm1, xmmword ptr [eax+16]",
          "movaps xmm2, xmm0", "minps xmm2, xmm1",
          "movaps xmm3, xmm0", "maxps xmm3, xmm1"], _SSE, "sse"),
    Case("sse_bitwise",
         "ANDNPS is ~dst & src, not dst & ~src",
         ["movaps xmm0, xmmword ptr [eax]",
          "movaps xmm1, xmmword ptr [eax+16]",
          "movaps xmm2, xmm0", "andps xmm2, xmm1",
          "movaps xmm3, xmm0", "orps xmm3, xmm1",
          "movaps xmm4, xmm0", "xorps xmm4, xmm1",
          "movaps xmm5, xmm0", "andnps xmm5, xmm1"], _SSE, "sse"),
    Case("sse_compares",
         "CMPNEQPS is the unordered form, so it is TRUE on NaN while "
         "EQ/LT/LE are ordered and false",
         ["movaps xmm0, xmmword ptr [eax]",
          "movaps xmm1, xmmword ptr [eax+16]",
          "movaps xmm2, xmm0", "cmpeqps xmm2, xmm1",
          "movaps xmm3, xmm0", "cmpltps xmm3, xmm1",
          "movaps xmm4, xmm0", "cmpleps xmm4, xmm1",
          "movaps xmm5, xmm0", "cmpneqps xmm5, xmm1"], _SSE, "sse"),
    Case("sse_shuffle", "shufps takes lanes 0-1 from dst and 2-3 from src",
         ["movaps xmm0, xmmword ptr [eax]",
          "movaps xmm1, xmmword ptr [eax+16]",
          "shufps xmm0, xmm1, 27"], _SSE, "sse"),
    Case("sse_unpack", "unpcklps / unpckhps interleave",
         ["movaps xmm0, xmmword ptr [eax]",
          "movaps xmm1, xmmword ptr [eax+16]",
          "movaps xmm2, xmm0", "unpcklps xmm2, xmm1",
          "movaps xmm3, xmm0", "unpckhps xmm3, xmm1"], _SSE, "sse"),
    Case("sse_halves", "movlps/movhps/movlhps/movhlps move one half",
         ["movaps xmm0, xmmword ptr [eax]",
          "movaps xmm1, xmmword ptr [eax+16]",
          "movlps xmm0, qword ptr [eax+16]",
          "movhps xmm1, qword ptr [eax]",
          "movaps xmm2, xmm0", "movlhps xmm2, xmm1",
          "movaps xmm3, xmm0", "movhlps xmm3, xmm1"], _SSE, "sse"),
    Case("sse_scalar_move",
         "movss from memory zeroes the upper three lanes; "
         "register-to-register leaves them alone",
         ["movaps xmm1, xmmword ptr [eax+16]",
          "movss xmm0, dword ptr [eax]",
          "movss xmm1, xmm0"], _SSE, "sse"),
    Case("sse_movmskps", "movmskps packs the four lane sign bits into eax",
         ["movaps xmm0, xmmword ptr [eax]", "movmskps eax, xmm0"],
         _SSE, "sse"),
    Case("sse_zero_idiom", "xorps xmm,xmm is the zeroing idiom",
         ["movaps xmm0, xmmword ptr [eax]", "xorps xmm0, xmm0"], _SSE, "sse"),

    # SF is the sign bit at the operand's width. Evaluating an 8- or 16-bit
    # test as int32 makes 0x80..0xFF look positive, so the branch goes the same
    # way regardless -- found by the whole-function corpus, kept here so the
    # unit-level suite catches it too.
    Case("jns_i8", "jns after an 8-bit test reads bit 7, not bit 31",
         ["test al, al", "setns al", "movzx eax, al"], _PAIRS),
    Case("js_i8", "js after an 8-bit test",
         ["test al, cl", "sets al", "movzx eax, al"], _PAIRS),
    Case("jns_i16", "jns after a 16-bit test reads bit 15",
         ["test ax, ax", "setns al", "movzx eax, al"], _PAIRS),
    Case("js_cmp_i8", "sign flag after an 8-bit cmp, which truncates first",
         ["cmp al, cl", "sets al", "movzx eax, al"], _PAIRS),
    Case("js_add_i8", "sign flag after an 8-bit add reads bit 7 of the result",
         ["add al, cl", "sets al", "movzx eax, al"], _PAIRS),
    Case("js_sub_i16", "sign flag after a 16-bit sub reads bit 15",
         ["sub ax, cx", "sets al", "movzx eax, al"], _PAIRS),
    Case("js_xor_i8", "sign flag after an 8-bit xor",
         ["xor al, cl", "sets al", "movzx eax, al"], _PAIRS),
    Case("js_sar_i8", "sign flag after an 8-bit sar",
         ["sar al, 1", "sets al", "movzx eax, al"], _PAIRS),

    # Unsigned word saturation must clamp each lane, not wrap or drop the op.
    *[Case("mmx_" + op + "_" + form + "_" + str(half),
           "unsigned word arithmetic saturates all four lanes",
           ["movd mm0, eax", "punpckldq mm0, mm0",
            "movd mm1, ecx", "punpckldq mm1, mm1"]
           + ([op + " mm0, mm1"] if form == "reg" else
              ["sub esp, 8", "movq qword ptr [esp], mm1",
               op + " mm0, qword ptr [esp]", "add esp, 8"])
           + (["psrlq mm0, 32"] if half else [])
           + ["sub esp, 8", "movq qword ptr [esp], mm0",
              "mov eax, dword ptr [esp]", "add esp, 8", "emms"],
           [(a, b) for a in (0, 1, 0x0001ffff, 0x80007fff, 0xfffeffff, 0xffffffff)
                   for b in (0, 1, 0x0001ffff, 0x80007fff, 0xfffeffff, 0xffffffff)])
      for op in ("paddusw", "psubusw") for form in ("reg", "mem") for half in (0, 1)],

    *[Case("mmx_" + op + "_flags", "unsigned saturated arithmetic preserves EFLAGS",
           ["pxor mm0, mm0", "pxor mm1, mm1", "cmp eax, 0", op + " mm0, mm1",
            "sete al", "movzx eax, al", "emms"], [(0, 0), (1, 0)])
      for op in ("paddusw", "psubusw")],

    # bt/btr/bts/btc are 386 instructions, so real Xbox code has them. They
    # were unhandled until the corpus lifted the CRT's float-to-int helper,
    # which uses btr to clear a rounding-control bit of the x87 control word.
    Case("bt_read", "bt sets CF from the selected bit",
         ["bt eax, ecx", "sbb eax, eax"], _PAIRS),
    Case("btr_clear", "btr reads the bit and then clears it",
         ["btr eax, 5"], _PAIRS),
    Case("bts_set", "bts sets it", ["bts eax, 5"], _PAIRS),
    Case("btc_flip", "btc complements it", ["btc eax, 5"], _PAIRS),
    Case("btr_reg", "btr with a register bit index, which is taken mod 32",
         ["btr eax, ecx"], _PAIRS),

    # FPREM/FPREM1 finish in one step here, so C2 (incomplete) must be clear
    # afterwards even when an fxam set it before (sp00n f69a435).
    Case("fpu_fprem_completion", "FPREM clears FXAM's C2 after complete reduction",
         ["fld qword ptr [eax+16]", "fld qword ptr [eax]", "fxam",
          "fprem", "fnstsw ax", "and eax, 0400h"],
         [(7.5, 3.0), (-7.5, 3.0), (7.5, -3.0), (0.0, 3.0),
          (100.0, 7.0), (2.5, 2.0)], "fpu"),
    Case("fpu_fprem1_completion", "FPREM1 clears C2 and rounds the quotient to nearest",
         ["fld qword ptr [eax+16]", "fld qword ptr [eax]", "fxam",
          "fprem1", "fnstsw ax", "and eax, 0400h"],
         [(7.5, 3.0), (-7.5, 3.0), (7.5, -3.0), (0.0, 3.0),
          (100.0, 7.0), (2.5, 2.0)], "fpu"),

    # FPREM/FPREM1 report the quotient's low three bits in C0/C3/C1, which the
    # CRT's sin/cos reduction picks its octant from (sp00n 999a8c6; new here,
    # upstream checks it through fpdiff).
    Case("fpu_fprem_quotient_bits", "FPREM: C0/C3/C1 hold the chopped quotient's bits",
         ["fld qword ptr [eax+16]", "fld qword ptr [eax]",
          "fprem", "fnstsw ax", "and eax, 04700h"],
         [(7.5, 3.0), (-7.5, 3.0), (7.5, -3.0), (0.0, 3.0), (100.0, 7.0),
          (2.5, 2.0), (29.0, 2.0), (1e10, 1.25), (1.0, 3.0)], "fpu"),
    Case("fpu_fprem1_quotient_bits", "FPREM1: the same for the rounded quotient",
         ["fld qword ptr [eax+16]", "fld qword ptr [eax]",
          "fprem1", "fnstsw ax", "and eax, 04700h"],
         [(7.5, 3.0), (-7.5, 3.0), (7.5, -3.0), (0.0, 3.0), (100.0, 7.0),
          (2.5, 2.0), (29.0, 2.0), (1e10, 1.25), (1.0, 3.0)], "fpu"),
    Case("fpu_fscale", "fscale: st0 * 2^trunc(st1)",
         ["fld qword ptr [eax+16]", "fld qword ptr [eax]", "fscale"],
         [(1.5, 3.7), (1.5, -3.7), (-1.0, 10.0), (3.0, 0.5), (0.0, 5.0),
          (1.0, -1074.0), (1.0, 1023.0)], "fpu"),

    # An x87 compare against a NaN is unordered: C3, C2 and C0 all set. The
    # model reported "equal", so `fucompp; fnstsw ax; test ah,44h; jp` -- how
    # this era's CRT asks "is this a NaN" -- answered no every time. Found by
    # running Crimson Skies' own float classification against itself.
    Case("fpu_nan_unordered",
         "comparing a value with itself is the isnan idiom; NaN is unordered",
         ["fld qword ptr [eax]", "fld qword ptr [eax]", "fucompp",
          "fnstsw ax", "and eax, 04500h"], _FP_NAN, "fpu"),
    Case("fpu_nan_vs_number", "NaN against an ordinary value is also unordered",
         ["fld qword ptr [eax+16]", "fld qword ptr [eax]", "fucompp",
          "fnstsw ax", "and eax, 04500h"], _FP_NAN, "fpu"),

    # EFLAGS from fcomi and sahf are a snapshot (sp00n bb2b5c7; upstream checks
    # them with its fpdiff harness, which option-c does not have). An fcom
    # between fcomi and the reader must not change CF/ZF/PF, and sahf loads
    # them from AH -- here FXAM's class bits, which no compare produced.
    Case("fpu_fcomi_survives_fcom", "fcomi's flags outlive a later fcom",
         ["fld qword ptr [eax+16]", "fld qword ptr [eax]", "fcomi st(0), st(1)",
          "fldz", "fcomp st(1)", "setb cl", "setz ch", "setp dl",
          "movzx eax, cx", "movzx edx, dl", "shl edx, 16", "or eax, edx"],
         _FP_NAN, "fpu"),
    # fstp tbyte stored nothing and fld tbyte read a float (sp00n b40805c).
    Case("fpu_tbyte_round_trip", "fstp tbyte then fld tbyte gives the value back",
         ["fld qword ptr [eax]", "fstp tbyte ptr [eax+32]",
          "fld tbyte ptr [eax+32]"], _FP, "fpu"),

    # ficom/ficomp were comments: stale status word, no pop (sp00n 2d38bcd).
    Case("fpu_ficom_m32", "ficom compares st0 with a dword integer",
         ["fld qword ptr [eax+16]", "fistp dword ptr [eax+32]",
          "fld qword ptr [eax]", "ficom dword ptr [eax+32]",
          "fnstsw ax", "and eax, 04500h"], _FP, "fpu"),
    Case("fpu_ficomp_m16", "ficomp compares with a word integer and pops",
         ["fld qword ptr [eax+16]", "fistp word ptr [eax+32]",
          "fld qword ptr [eax]", "ficomp word ptr [eax+32]",
          "fnstsw ax", "and eax, 04500h"], _FP, "fpu"),

    # FCMOVcc was a comment (sp00n 01064f3); this is MSVC's float min.
    Case("fpu_fcmov_min", "fcmove after fcom/fnstsw/test ah,1 keeps the minimum",
         ["fld qword ptr [eax+16]", "fld qword ptr [eax]", "fcom st(1)",
          "fnstsw ax", "test ah, 1", "fcmove st(0), st(1)", "fxch st(1)",
          "fstp st(0)"], _FP, "fpu"),
    Case("fpu_sahf_from_fxam", "sahf loads CF/ZF/PF from AH, not from a compare",
         ["fld qword ptr [eax]", "fxam", "fnstsw ax", "sahf",
          "setb cl", "setz ch", "setp dl",
          "movzx eax, cx", "movzx edx, dl", "shl edx, 16", "or eax, edx"],
         _FP_NAN, "fpu"),
]

# MMX arithmetic/conversions preserve EFLAGS. Exercise the deferred condition,
# not only the MMX result, with both true and false incoming comparisons.
for _op in (
        "paddsb mm0, mm1", "paddsw mm0, mm1", "paddusb mm0, mm1",
        "psubsb mm0, mm1", "psubsw mm0, mm1", "psubusb mm0, mm1",
        "pavgb mm0, mm1", "pavgw mm0, mm1", "pminsw mm0, mm1",
        "pmaxsw mm0, mm1", "psadbw mm0, mm1",
        "cvtps2pi mm0, xmm0", "cvttps2pi mm0, xmm0",
        "pinsrw mm0, ecx, 0", "pextrw edx, mm0, 0"):
    for _consumer, _before, _after in (
            ("sete", ["cmp eax, 0"], ["sete al", "movzx eax, al"]),
            ("cmove", ["cmp eax, 0"], ["cmove eax, ecx"]),
            ("sbb", ["neg eax"], ["sbb eax, eax"])):
        CASES.append(Case(
            "mmx_flags_" + _op.split()[0] + "_" + _consumer,
            "MMX preserves the comparison/carry consumed afterward",
            ["pxor mm0, mm0", "pxor mm1, mm1", "xorps xmm0, xmm0"]
            + _before + [_op] + _after + ["emms"], [(0, 7), (1, 7)]))

CASES.append(Case("pushad_popad", "POPAD restores registers and skips saved ESP",
    ["pushad", "mov eax, 0", "mov ecx, 0", "popad", "add eax, ecx"], _PAIRS))

# Exercise ordinary-RAM REP semantics against the real CPU. MMIO side effects
# are covered by the access-counting fixture, since host RAM is not a device.
for _name, _setup, _count, _copy, _result in (
        ("rep_copy_byte", ["mov esi, eax", "lea edi, [eax+16]"], 4, "rep movsb", "mov eax, [edi-4]"),
        ("rep_copy_word", ["mov esi, eax", "lea edi, [eax+16]"], 4, "rep movsw", "mov eax, [edi-8]"),
        ("rep_copy_dword", ["mov esi, eax", "lea edi, [eax+16]"], 4, "rep movsd", "mov eax, [edi-16]"),
        ("rep_copy_overlap", ["mov esi, eax", "lea edi, [eax+4]"], 3, "rep movsd", "mov eax, [edi-4]"),
        ("rep_copy_backward", ["lea esi, [eax+12]", "lea edi, [eax+28]", "std"], 4, "rep movsd", "mov eax, [edi+4]"),
        ("rep_copy_zero", ["mov esi, eax", "lea edi, [eax+16]"], 0, "rep movsd", "mov eax, [edi]")):
    CASES.append(Case(_name, "REP copy preserves element order, direction and count",
        ["cld"] + _setup + ["mov ecx, %d" % _count, _copy, "cld", _result], _SSE, "sse"))

# -- C gate, G1 (2026-10-24): unaligned guest accesses ------------------------
# C.2/F3 made guest accesses plain `aligned(1), may_alias` (no blanket
# volatile): a lift that let the compiler assume natural alignment, or merge
# and reorder accesses across a store, would read the wrong bytes here. eax is
# the scratch buffer, 64-byte aligned and 128 bytes long (A at +0, B at +16,
# free from +32), so +60..+67 straddles a cache line.
for _off in (1, 2, 3, 13):
    CASES.append(Case(f"ua_load32_off{_off}", "dword load at an unaligned address",
                      [f"mov eax, [eax+{_off}]"], _SSE, "sse"))
CASES.extend([
    Case("ua_load16", "word loads at odd addresses, zero- and sign-extended",
         ["movzx ecx, word ptr [eax+1]", "movsx edx, word ptr [eax+15]",
          "lea eax, [ecx+edx*2]"], _SSE, "sse"),
    Case("ua_store32_line", "dword stores across a cache line, read back at other offsets",
         ["mov ecx, [eax+16]", "mov [eax+62], ecx", "mov edx, [eax+1]",
          "mov [eax+65], edx", "mov ecx, [eax+60]", "mov edx, [eax+64]",
          "xor ecx, edx", "mov edx, [eax+66]", "add ecx, edx", "mov eax, ecx"],
         _SSE, "sse"),
    Case("ua_store16", "word store at an odd address, read back overlapping",
         ["mov cx, [eax+3]", "mov [eax+63], cx", "movzx eax, word ptr [eax+62]"],
         _SSE, "sse"),
    Case("ua_rmw", "read-modify-write at unaligned addresses, 32/16/8 bits",
         ["mov ecx, [eax+16]", "add [eax+1], ecx", "sub [eax+6], cx",
          "xor byte ptr [eax+7], cl", "mov ecx, [eax+1]", "mov edx, [eax+5]",
          "lea eax, [ecx+edx]"], _SSE, "sse"),
    Case("ua_sse", "movups/movss/movlps/movhps at unaligned addresses",
         ["movups xmm0, [eax+1]", "movups [eax+51], xmm0", "movups xmm1, [eax+49]",
          "movss xmm2, [eax+3]", "movlps xmm3, [eax+5]", "movhps xmm3, [eax+11]"],
         _SSE, "sse"),
    Case("ua_x87", "x87 qword/dword stores and loads at odd addresses",
         ["fld qword ptr [eax]", "fstp qword ptr [eax+33]", "fld qword ptr [eax+33]",
          "fld qword ptr [eax+16]", "fstp dword ptr [eax+45]", "fld dword ptr [eax+45]",
          "faddp st(1), st"], _FP, "fpu"),
])

# -- C gate, G1: fences and locked read-modify-writes --------------------------
# F2 lifts sfence/lfence/mfence to __atomic_thread_fence (test_lifter_fences
# checks the emitted C); the ordering itself is not observable single-threaded,
# so these pin the values and flags around them. The polling side (a second
# thread) is tools/recomp/test_g1_spin_forms.py.
CASES.extend([
    Case("fence_store_load", "stores and loads around sfence/mfence/lfence keep their values",
         ["mov ecx, [eax+16]", "mov [eax+32], ecx", "sfence", "mov edx, [eax+4]",
          "mov [eax+36], edx", "mfence", "mov ecx, [eax+32]", "lfence",
          "add ecx, [eax+36]", "mov eax, ecx"], _SSE, "sse"),
    Case("lock_xadd", "lock xadd: old value to the register, sum to memory, flags of the add",
         ["mov ecx, [eax]", "mov [eax+32], ecx", "mov ecx, [eax+16]",
          "lock xadd [eax+32], ecx", "setc dl", "setz dh", "movzx edx, dx",
          "add ecx, [eax+32]", "lea eax, [ecx+edx*4]"], _SSE, "sse"),
    Case("lock_cmpxchg_hit", "lock cmpxchg with a matching comparand stores and sets ZF",
         ["mov esi, eax", "mov ecx, [esi+16]", "mov eax, [esi]", "mov [esi+32], eax",
          "lock cmpxchg [esi+32], ecx", "setz dl", "movzx edx, dl", "mov ecx, [esi+32]",
          "lea eax, [eax+ecx*2]", "add eax, edx"], _SSE, "sse"),
    Case("lock_cmpxchg_miss", "a different comparand loads memory into eax and clears ZF",
         ["mov esi, eax", "mov ecx, [esi+16]", "mov eax, [esi]", "mov [esi+32], eax",
          "mov eax, [esi+4]", "lock cmpxchg [esi+32], ecx", "setz dl", "movzx edx, dl",
          "mov ecx, [esi+32]", "lea eax, [eax+ecx*2]", "add eax, edx"], _SSE, "sse"),
    Case("xchg_mem", "xchg with memory (implicitly locked) swaps both ways",
         ["mov ecx, [eax+16]", "mov edx, [eax]", "mov [eax+32], edx",
          "xchg [eax+32], ecx", "mov edx, [eax+32]", "lea eax, [ecx+edx*2]"], _SSE, "sse"),
    Case("xchg_mem_narrow", "xchg with byte and word memory, high-byte register and reversed operands",
         ["mov ecx, [eax+16]", "mov edx, [eax]", "mov [eax+32], edx",
          "xchg [eax+33], ch", "xchg cx, [eax+34]", "xchg [eax+32], cl",
          "mov edx, [eax+32]", "lea eax, [ecx+edx*2]"], _SSE, "sse"),
    Case("lock_refcount", "lock dec/inc/or/and/add on memory and the ZF a refcount release reads",
         ["mov dword ptr [eax+32], 2", "lock dec dword ptr [eax+32]", "setz cl",
          "lock dec dword ptr [eax+32]", "setz ch", "lock inc dword ptr [eax+32]",
          "lock or dword ptr [eax+32], 0x100", "lock and dword ptr [eax+32], 0x1FF",
          "lock add dword ptr [eax+32], 3", "movzx ecx, cx", "mov eax, [eax+32]",
          "shl eax, 16", "or eax, ecx"], _SSE[:2], "sse"),
])

# -- C gate, G1: pause (the spin-wait hint) changes no register or flag -------
CASES.append(Case("pause_keeps_state", "pause between a compare and its consumer changes nothing",
                  ["cmp eax, ecx", "pause", "setl dl", "pause", "movzx eax, dl", "add eax, ecx"], _PAIRS))

# -- C gate, G1: shift counts (THPS; c1ac219) beyond the first cases --
CASES.extend([
    Case("sal_cl_wide", "sal is shl: counts >= 32 masked to 5 bits",
         ["sal eax, cl"], _SHIFT_COUNTS),
    Case("shl_mem_cl", "shl of a memory operand by cl, counts >= 32",
         ["push eax", "shl dword ptr [esp], cl", "pop eax"], _SHIFT_COUNTS),
    Case("shr_mem16_cl", "16-bit shr of a memory operand by cl",
         ["push eax", "shr word ptr [esp], cl", "pop eax"], _SHIFT_COUNTS),
    Case("shl_count32_keeps_flags", "a count that masks to 0 leaves the flags alone",
         ["test ecx, ecx", "shl eax, cl", "setz dl", "movzx edx, dl",
          "lea eax, [eax+edx]"], _SHIFT_COUNTS),
])
CASES.extend([
    Case("lock_xadd_zero_carry", "lock xadd whose sum wraps to 0: ZF and CF set, as the add sets them",
         ["mov ecx, [eax]", "mov [eax+32], ecx", "neg ecx", "lock xadd [eax+32], ecx",
          "setc dl", "setz dh", "movzx edx, dx", "add ecx, [eax+32]",
          "lea eax, [ecx+edx*4]"], _SSE, "sse"),
])

# MMX helpers on SSE2 (recomp_types.h, 2026-10-25): every helper that has an SSE2 form, and for the ones whose upper
# dword carries the second operand (high unpacks, packs) or lanes 2/3, the upper dword as well -- the cases above
# report dword 0 only. The register-count shifts take the count from mm1, so the _PAIRS edges give counts past the
# lane width and huge 64-bit counts.
def _mmx_case(name, why, op, high=False):
    load = ['push eax', 'push ecx', 'push eax', 'push ecx', 'movq mm0, qword ptr [esp]',
            'push eax', 'push eax', 'push ecx', 'push ecx', 'movq mm1, qword ptr [esp]', 'add esp, 32']
    if op.startswith('pmovmskb'):
        return Case(name, why, load + [op, 'emms'], _PAIRS)
    return Case(name, why, load + [op, 'push 0', 'push 0', 'movq qword ptr [esp], mm0',
                                   'mov eax, dword ptr [esp+%d]' % (4 if high else 0), 'add esp, 8', 'emms'], _PAIRS)


_MMX_SSE2 = [
    ("paddb", "paddb mm0, mm1"), ("psubb", "psubb mm0, mm1"), ("psubd", "psubd mm0, mm1"),
    ("paddsb", "paddsb mm0, mm1"), ("psubsb", "psubsb mm0, mm1"), ("psubsw", "psubsw mm0, mm1"),
    ("paddusb", "paddusb mm0, mm1"), ("psubusb", "psubusb mm0, mm1"),
    ("paddusw", "paddusw mm0, mm1"), ("psubusw", "psubusw mm0, mm1"),
    ("pavgw", "pavgw mm0, mm1"), ("pminsw", "pminsw mm0, mm1"), ("pmaxsw", "pmaxsw mm0, mm1"),
    ("pcmpeqb", "pcmpeqb mm0, mm1"), ("pcmpeqw", "pcmpeqw mm0, mm1"), ("pcmpeqd", "pcmpeqd mm0, mm1"),
    ("pcmpgtb", "pcmpgtb mm0, mm1"), ("pcmpgtd", "pcmpgtd mm0, mm1"),
    ("psllw", "psllw mm0, 3"), ("psrld", "psrld mm0, 9"), ("psllq", "psllq mm0, 13"), ("psrad", "psrad mm0, 5"),
    ("psrad_big", "psrad mm0, 40"), ("psllq_big", "psllq mm0, 70"),
    ("psraw_reg", "psraw mm0, mm1"), ("psrlw_reg", "psrlw mm0, mm1"), ("pslld_reg", "pslld mm0, mm1"),
    ("psrlq_reg", "psrlq mm0, mm1"), ("psad", "psadbw mm0, mm1"), ("pmovmskb", "pmovmskb eax, mm0"),
    ("punpckhwd", "punpckhwd mm0, mm1"), ("punpckldq", "punpckldq mm0, mm1"), ("packsswb", "packsswb mm0, mm1"),
]
_MMX_SSE2_HIGH = [
    ("punpcklbw", "punpcklbw mm0, mm1"), ("punpckhbw", "punpckhbw mm0, mm1"), ("punpcklwd", "punpcklwd mm0, mm1"),
    ("punpckhwd", "punpckhwd mm0, mm1"), ("punpckldq", "punpckldq mm0, mm1"), ("punpckhdq", "punpckhdq mm0, mm1"),
    ("packsswb", "packsswb mm0, mm1"), ("packuswb", "packuswb mm0, mm1"), ("packssdw", "packssdw mm0, mm1"),
    ("pmaddwd", "pmaddwd mm0, mm1"), ("pmulhw", "pmulhw mm0, mm1"), ("pmullw", "pmullw mm0, mm1"),
    ("paddsw", "paddsw mm0, mm1"), ("pcmpgtw", "pcmpgtw mm0, mm1"), ("pavgb", "pavgb mm0, mm1"),
    ("psraw_reg", "psraw mm0, mm1"), ("psrlq_reg", "psrlq mm0, mm1"), ("psad", "psadbw mm0, mm1"),
]
CASES.extend([_mmx_case("mmx_" + n, "SSE2 helper: " + op, op) for n, op in _MMX_SSE2])
CASES.extend([_mmx_case("mmx_%s_hi" % n, "SSE2 helper, upper dword: " + op, op, True) for n, op in _MMX_SSE2_HIGH])
