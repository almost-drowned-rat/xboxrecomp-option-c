"""Option C, C.2: guest accesses in game code are plain (not volatile), so a lifted
polling loop must still see a store another host thread makes -- the compiler
barrier on backward branches (RECOMP_BACKEDGE) is what guarantees it.

Lifts `spin: mov eax, [ecx]; test eax, eax; jz spin; ret` (in .text), compiles it
with the real runtime header at -O2 and runs it while a second thread stores a
non-zero value after 50 ms. Without the barrier clang hoists the load out of the
loop and the program spins until the watchdog kills it.
"""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

from . import config
from .translator import FunctionTranslator

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r"""
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

ptrdiff_t g_xbox_mem_offset;
static uint8_t g_mem[0x20000];

BODY

static void *writer(void *arg) {
    (void)arg;
#ifdef _WIN32
    Sleep(50);
#else
    usleep(50000);
#endif
    __atomic_store_n((uint32_t *)(g_mem + 0x1000), 0x2Au, __ATOMIC_RELEASE);
    return NULL;
}
static void *watchdog(void *arg) {
    (void)arg;
#ifdef _WIN32
    Sleep(5000);
#else
    sleep(5);
#endif
    printf("TIMEOUT: the lifted loop never saw the other thread's store\n");
    fflush(stdout);
    _exit(2);
    return NULL;
}
#ifdef _WIN32
static DWORD WINAPI w_writer(LPVOID a) { writer(a); return 0; }
static DWORD WINAPI w_watchdog(LPVOID a) { watchdog(a); return 0; }
#endif

int main(void) {
    X86Ctx c = {0};
    X86Ctx *ctx = &c;
    g_ecx = 0x1000;
    g_esp = 0x8000;
#ifdef _WIN32
    CloseHandle(CreateThread(NULL, 0, w_watchdog, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, w_writer, NULL, 0, NULL));
#else
    pthread_t t1, t2;
    pthread_create(&t1, NULL, watchdog, NULL);
    pthread_create(&t2, NULL, writer, NULL);
#endif
    spin(ctx, g_mem);
    if (g_eax != 0x2Au || g_esp != 0x8004) { printf("FAIL eax=%08X esp=%08X\n", g_eax, g_esp); return 1; }
    printf("OK\n");
    return 0;
}
"""


def _cc():
    return shutil.which("clang") or shutil.which("gcc")


def test_lifted_polling_loop_sees_another_threads_store_at_O2():
    cc = _cc()
    if not cc:
        pytest.skip("no C compiler on PATH")
    image = bytes.fromhex("8b01 85c0 74fa c3".replace(" ", ""))
    base = 0x10000
    config._install([config.Section(".text", base, len(image), 0, len(image), True)],
                    entry_point=base, kernel_thunk_addr=base, origin="c2-spin-wait")
    db = {base: {"start": hex(base), "end": base + len(image), "_addr": base, "size": len(image),
                 "section": ".text"}}
    body = FunctionTranslator(image, db).translate_function(base, db[base])
    assert "RECOMP_BACKEDGE" in body, body
    assert "RECOMP_MEMQ volatile" not in body, "game code (.text) must get plain accesses"
    src = HARNESS.replace("BODY", body.replace("sub_00010000", "spin"))
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "spin.c"
        exe = Path(tmp) / "spin.exe"
        c.write_text(src, encoding="utf-8")
        built = subprocess.run([cc, "-O2", "-w", "-I", str(ROOT / "templates" / "runtime"), str(c), "-o", str(exe)]
                               + ([] if shutil.which("clang") and "clang" in cc and _is_windows() else ["-lpthread"]),
                               capture_output=True, text=True)
        assert built.returncode == 0, built.stderr[-3000:]
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        assert ran.returncode == 0 and "OK" in ran.stdout, ran.stdout + ran.stderr


def test_library_section_functions_keep_volatile_accesses():
    image = bytes.fromhex("8b01 85c0 74fa c3".replace(" ", ""))
    base = 0x10000
    config._install([config.Section("D3D", base, len(image), 0, len(image), True)],
                    entry_point=base, kernel_thunk_addr=base, origin="c2-library")
    db = {base: {"start": hex(base), "end": base + len(image), "_addr": base, "size": len(image),
                 "section": "D3D"}}
    body = FunctionTranslator(image, db).translate_function(base, db[base])
    assert "#define RECOMP_MEMQ volatile" in body, body
    assert "#define RECOMP_GPTR(addr) RECOMP_LIB_GPTR(addr)" in body, body
    assert body.rstrip().endswith("#define RECOMP_GPTR(addr) RECOMP_GAME_GPTR(addr)"), body[-200:]


def _is_windows():
    import os
    return os.name == "nt"


def test_negative_control_without_the_barrier_the_loop_spins_forever():
    """The test above must be able to fail: with RECOMP_BACKEDGE switched off,
    clang -O2 keeps the guest load out of the loop and the watchdog fires."""
    cc = _cc()
    if not cc:
        pytest.skip("no C compiler on PATH")
    image = bytes.fromhex("8b0185c074fac3")
    base = 0x10000
    config._install([config.Section(".text", base, len(image), 0, len(image), True)],
                    entry_point=base, kernel_thunk_addr=base, origin="c2-spin-wait-neg")
    db = {base: {"start": hex(base), "end": base + len(image), "_addr": base, "size": len(image),
                 "section": ".text"}}
    body = FunctionTranslator(image, db).translate_function(base, db[base])
    no_barrier = "#undef RECOMP_BACKEDGE\n#define RECOMP_BACKEDGE() ((void)0)\n"
    src = HARNESS.replace("BODY", no_barrier + body.replace("sub_00010000", "spin"))
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "spin.c"
        exe = Path(tmp) / "spin.exe"
        c.write_text(src, encoding="utf-8")
        built = subprocess.run([cc, "-O2", "-w", "-I", str(ROOT / "templates" / "runtime"), str(c), "-o", str(exe)]
                               + ([] if _is_windows() else ["-lpthread"]), capture_output=True, text=True)
        assert built.returncode == 0, built.stderr[-3000:]
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        assert ran.returncode == 2 and "TIMEOUT" in ran.stdout, ran.stdout + ran.stderr
