"""C gate, G1 sixth finding (2026-10-24): xchg with a memory operand is locked
on x86 with or without the prefix -- the spin-lock acquire and
InterlockedExchange. It used to lift as two plain stores, so two host threads
could both read the lock as free and both enter.

The lifted spin lock `L: mov eax, 1; xchg [ecx], eax; test eax, eax; jnz L;
inc dword ptr [ecx+4]; mov dword ptr [ecx], 0; dec edx; jnz L; ret` runs on
four host threads at once, 200000 rounds each, at -O0 and -O2: the counter it
guards (a plain inc) must come out exact. The single-threaded values are
covered by the conformance case xchg_mem.
"""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

from . import config
from .disasm import BasicBlock, Disassembler
from .lifter import Lifter, lift_basic_block
from .test_g1_lock_atomic import HARNESS
from .test_spin_wait_c2 import ROOT, _is_windows
from .translator import FunctionTranslator

# L: mov eax, 1 / xchg [ecx], eax / test eax, eax / jnz L / inc dword [ecx+4] /
#    mov dword [ecx], 0 / dec edx / jnz L / ret
CODE = "b801000000 8701 85c0 75f5 ff4104 c70100000000 4a 75e9 c3"
N, THREADS = 200000, 4


def _cc():
    return shutil.which("clang") or shutil.which("gcc")


def _lift():
    image = bytes.fromhex(CODE.replace(" ", ""))
    base = 0x10000
    config._install([config.Section(".text", base, len(image), 0, len(image), True)],
                    entry_point=base, kernel_thunk_addr=base, origin="g1-xchg-atomic")
    db = {base: {"start": hex(base), "end": base + len(image), "_addr": base, "size": len(image),
                 "section": ".text"}}
    return FunctionTranslator(image, db).translate_function(base, db[base])


def _block(hexcode):
    d = Disassembler()
    insns = [d._decode_instruction(i) for i in d._cs.disasm(bytes.fromhex(hexcode), 0x1000)]
    lifted, _ = lift_basic_block(Lifter(), BasicBlock(start=0x1000, instructions=insns))
    return "\n".join(lifted)


@pytest.mark.parametrize("hexcode, macro, write", [
    ("8701", "RECOMP_ATOMIC_XCHG32(", "eax = _xc_o"),          # xchg [ecx], eax
    ("8708", "RECOMP_ATOMIC_XCHG32(", "ecx = _xc_o"),          # xchg [eax], ecx (address reg = other operand)
    ("8601", "RECOMP_ATOMIC_XCHG8(", "_xc_o"),                 # xchg [ecx], al
    ("668701", "RECOMP_ATOMIC_XCHG16(", "_xc_o"),              # xchg [ecx], ax
    ("f08701", "RECOMP_ATOMIC_XCHG32(", "eax = _xc_o"),        # lock xchg [ecx], eax
])
def test_xchg_with_memory_is_one_atomic_exchange(hexcode, macro, write):
    text = _block(hexcode)
    assert macro in text and write in text, text
    assert "RECOMP_UNIMPL" not in text and "_tmp" not in text, text


def test_xchg_between_registers_is_unchanged():
    text = _block("87c8")                                      # xchg eax, ecx
    assert "RECOMP_ATOMIC" not in text and "_tmp" in text, text


@pytest.mark.parametrize("opt", ["-O0", "-O2"])
def test_four_threads_never_share_the_lock(opt):
    cc = _cc()
    if not cc:
        pytest.skip("no C compiler on PATH")
    body = _lift()
    assert "RECOMP_ATOMIC_XCHG32(" in body, body
    # The harness checks the word at +0 (here: the lock, released to 0) and
    # +4 (the guarded counter); check what this loop leaves there.
    src = (HARNESS.replace("BODY", body.replace("sub_00010000", "loop"))
           .replace("NITER", str(N)).replace("NTHREADS", str(THREADS))
           .replace("inc == (uint32_t)NITER * NTHREADS && add == 3u * NITER * NTHREADS".replace(
               "NITER", str(N)).replace("NTHREADS", str(THREADS)),
               f"inc == 0u && add == (uint32_t){N} * {THREADS}"))
    assert f"add == (uint32_t){N} * {THREADS}" in src
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "xchg.c"
        exe = Path(tmp) / "xchg.exe"
        c.write_text(src, encoding="utf-8")
        built = subprocess.run([cc, opt, "-w", "-I", str(ROOT / "templates" / "runtime"), str(c), "-o", str(exe)]
                               + ([] if _is_windows() else ["-lpthread"]), capture_output=True, text=True)
        assert built.returncode == 0, built.stderr[-3000:]
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
        assert ran.returncode == 0, (opt, ran.stdout + ran.stderr)
