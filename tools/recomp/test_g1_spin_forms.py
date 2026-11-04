"""C gate, G1 (2026-10-24): back-edge spin. The polling forms a title uses to wait
for another thread (or the GPU), lifted as game code (.text: plain accesses, no
blanket volatile), compiled with the real runtime header at -O0 and -O2, and run
while a second host thread stores 0x2A to the polled dword after 50 ms. The
loop must see the store and return it in eax with esp balanced; the watchdog
(5 s) fails a loop the compiler turned into an endless one. The harness is the
one of test_spin_wait_c2.py, which also holds the negative control (no
RECOMP_BACKEDGE -> the -O2 loop never ends).
"""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

from . import config
from .test_spin_wait_c2 import HARNESS, ROOT, _is_windows
from .translator import FunctionTranslator

# ecx points at the polled dword; every form ends with eax = the value seen.
FORMS = {
    # mov eax,[ecx] / test eax,eax / jz spin / ret
    "load_test_jz": "8b01 85c0 74fa c3",
    # spin: cmp dword ptr [ecx],0 / je spin / mov eax,[ecx] / ret
    "cmp_mem_imm_je": "833900 74fb 8b01 c3",
    # spin: pause / mov eax,[ecx] / test eax,eax / jz spin / ret
    "pause_loop": "f390 8b01 85c0 74f8 c3",
    # spin: mov al,[ecx] / test al,al / jz spin / movzx eax,al / ret
    "byte_poll": "8a01 84c0 74fa 0fb6c0 c3",
    # mov edx,0x7FFFFFFF / L: mov eax,[ecx] / test eax,eax / jnz done / dec edx / jnz L / done: ret
    "counted_poll": "baffffff7f 8b01 85c0 7503 4a 75f7 c3",
}


def _cc():
    return shutil.which("clang") or shutil.which("gcc")


@pytest.mark.parametrize("opt", ["-O0", "-O2"])
@pytest.mark.parametrize("form", sorted(FORMS))
def test_lifted_polling_form_sees_another_threads_store(form, opt):
    cc = _cc()
    if not cc:
        pytest.skip("no C compiler on PATH")
    image = bytes.fromhex(FORMS[form].replace(" ", ""))
    base = 0x10000
    config._install([config.Section(".text", base, len(image), 0, len(image), True)],
                    entry_point=base, kernel_thunk_addr=base, origin="g1-spin-" + form)
    db = {base: {"start": hex(base), "end": base + len(image), "_addr": base, "size": len(image),
                 "section": ".text"}}
    body = FunctionTranslator(image, db).translate_function(base, db[base])
    assert "RECOMP_BACKEDGE" in body, body
    assert "RECOMP_UNIMPL" not in body, body
    assert "RECOMP_MEMQ volatile" not in body, "game code (.text) must get plain accesses"
    src = HARNESS.replace("BODY", body.replace("sub_00010000", "spin"))
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "spin.c"
        exe = Path(tmp) / "spin.exe"
        c.write_text(src, encoding="utf-8")
        built = subprocess.run([cc, opt, "-w", "-I", str(ROOT / "templates" / "runtime"), str(c), "-o", str(exe)]
                               + ([] if _is_windows() else ["-lpthread"]), capture_output=True, text=True)
        assert built.returncode == 0, built.stderr[-3000:]
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        assert ran.returncode == 0 and "OK" in ran.stdout, (form, opt, ran.stdout + ran.stderr)
