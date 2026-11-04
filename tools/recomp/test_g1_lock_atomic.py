"""C gate, G1 gap 1 (2026-10-24): lock-prefixed read-modify-writes on memory are
atomic. `lock inc/dec/add/...` used to be RECOMP_UNIMPL -- not executed at all --
and lifting them plain would race once guest threads run on host threads.

The lifted loop `L: lock inc dword ptr [ecx]; lock add dword ptr [ecx+4], 3;
dec edx; jnz L; ret` runs on four host threads at once, each with its own
register set, 200000 iterations each, at -O0 and -O2: the counters must come out
exact. The single-threaded values and flags are covered by the conformance cases
(lock_refcount).
"""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

from . import config
from .test_spin_wait_c2 import ROOT, _is_windows
from .translator import FunctionTranslator

# L: lock inc dword ptr [ecx] / lock add dword ptr [ecx+4], 3 / dec edx / jnz L / ret
CODE = "f0ff01 f083410403 4a 75f5 c3"
N, THREADS = 200000, 4

HARNESS = r"""
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

ptrdiff_t g_xbox_mem_offset;
static uint8_t g_mem[0x20000];

BODY

static void *worker(void *arg) {
    X86Ctx c = {0};
    X86Ctx *ctx = &c;
    (void)arg;
    g_ecx = 0x1000;
    g_edx = NITER;
    g_esp = 0x8000 + 0x1000 * (uint32_t)(uintptr_t)arg;
    loop(ctx, g_mem);
    return NULL;
}
#ifdef _WIN32
static DWORD WINAPI w_worker(LPVOID a) { worker(a); return 0; }
#endif

int main(void) {
    int i;
#ifdef _WIN32
    HANDLE t[NTHREADS];
    for (i = 0; i < NTHREADS; i++) t[i] = CreateThread(NULL, 0, w_worker, (LPVOID)(uintptr_t)(i + 1), 0, NULL);
    WaitForMultipleObjects(NTHREADS, t, TRUE, INFINITE);
#else
    pthread_t t[NTHREADS];
    for (i = 0; i < NTHREADS; i++) pthread_create(&t[i], NULL, worker, (void *)(uintptr_t)(i + 1));
    for (i = 0; i < NTHREADS; i++) pthread_join(t[i], NULL);
#endif
    {
        uint32_t inc = *(uint32_t *)(g_mem + 0x1000), add = *(uint32_t *)(g_mem + 0x1004);
        printf("inc %u add %u\n", inc, add);
        return !(inc == (uint32_t)NITER * NTHREADS && add == 3u * NITER * NTHREADS);
    }
}
"""


def _cc():
    return shutil.which("clang") or shutil.which("gcc")


def _lift(lock=True):
    image = bytes.fromhex(CODE.replace(" ", ""))
    if not lock:
        image = image.replace(b"\xf0", b"\x90")    # nop: same layout, same jump
    base = 0x10000
    config._install([config.Section(".text", base, len(image), 0, len(image), True)],
                    entry_point=base, kernel_thunk_addr=base, origin="g1-lock-atomic")
    db = {base: {"start": hex(base), "end": base + len(image), "_addr": base, "size": len(image),
                 "section": ".text"}}
    return FunctionTranslator(image, db).translate_function(base, db[base])


def test_locked_forms_lift_to_a_cas_loop_around_the_plain_form():
    body = _lift()
    assert "RECOMP_UNIMPL" not in body, body
    assert body.count("RECOMP_ATOMIC_CAS_OK32(") == 2, body
    assert "/* lock inc */" in body and "/* lock add */" in body


@pytest.mark.parametrize("opt", ["-O0", "-O2"])
def test_four_threads_lose_no_update(opt):
    cc = _cc()
    if not cc:
        pytest.skip("no C compiler on PATH")
    body = _lift()
    src = (HARNESS.replace("BODY", body.replace("sub_00010000", "loop"))
           .replace("NITER", str(N)).replace("NTHREADS", str(THREADS)))
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "lock.c"
        exe = Path(tmp) / "lock.exe"
        c.write_text(src, encoding="utf-8")
        built = subprocess.run([cc, opt, "-w", "-I", str(ROOT / "templates" / "runtime"), str(c), "-o", str(exe)]
                               + ([] if _is_windows() else ["-lpthread"]), capture_output=True, text=True)
        assert built.returncode == 0, built.stderr[-3000:]
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
        assert ran.returncode == 0, (opt, ran.stdout + ran.stderr)
