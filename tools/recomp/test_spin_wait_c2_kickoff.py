"""Option C, C.2: the release fence in front of the pushbuffer kickoff.

Game code writes pushbuffer words with plain stores; D3D (XDK library code) then
stores DMA_PUT at 0xFD800040; the executor thread reads PUT with acquire and then
the words. RECOMP_LIB_GPTR -- the guest-pointer form of XDK library code -- puts
a release fence in front of every access to the NV2A USER area.

1. Compiler check (deterministic, x86-64): a word stored before the kickoff and
   overwritten after it must still be stored before it. Without the fence clang
   -O2 drops the first store (dead-store elimination across the volatile PUT
   store) -- the executor would read the later value, or none. With the fence
   both stores stay, in order. This is the reordering the fence prevents on x86;
   x86 hardware itself never reorders stores with other stores (TSO), so the
   hardware half (dmb on ARM64) cannot be provoked here.
2. Producer/consumer run: a writer thread fills a buffer with plain stores and
   kicks off; a reader thread acquires PUT and checks every word. It passes on
   x86 with or without the fence (TSO again) -- it is the shape the ARM64 build
   has to pass, kept here so it runs everywhere.
"""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

WRITER = r"""
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
/* the game's pushbuffer word, the kickoff in XDK library form, the next word */
__attribute__((noinline)) void writer(RECOMP_PARAMS)
{
    (void)ctx;
    MEM32(0x1000u) = 0x11111111u;
    *(volatile recomp_u32 *)KICKOFF_PTR(0xFD800040u) = 0x1004u;
    MEM32(0x1000u) = 0x22222222u;
}
"""

PRODUCER = r"""
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#endif
ptrdiff_t g_xbox_mem_offset;
static uint8_t *g_mem;
enum { WORDS = 4096, ROUNDS = 2000 };

static void produce(RECOMP_PARAMS, uint32_t round)
{
    (void)ctx;
    for (uint32_t i = 0; i < WORDS; i++)
        MEM32(0x10000u + i * 4u) = round * 0x10001u + i;          /* plain game stores */
    *(volatile recomp_u32 *)RECOMP_LIB_GPTR(0xFD800040u) = round;  /* kickoff, library form */
}
static int g_bad;
static void *consumer(void *arg)
{
    (void)arg;
    for (uint32_t round = 1; round <= ROUNDS; round++) {
        while (__atomic_load_n((uint32_t *)(g_mem + 0xFD800040u), __ATOMIC_ACQUIRE) != round) { }
        for (uint32_t i = 0; i < WORDS; i++)
            if (((uint32_t *)(g_mem + 0x10000u))[i] != round * 0x10001u + i) { g_bad++; break; }
        __atomic_store_n((uint32_t *)(g_mem + 0xFD800044u), round, __ATOMIC_RELEASE);  /* GET */
    }
    return NULL;
}
#ifdef _WIN32
static DWORD WINAPI w_consumer(LPVOID a) { consumer(a); return 0; }
#endif
int main(void)
{
    X86Ctx c = {0};
    /* a sparse 4 GB guest window is not needed: map the two pages used */
#ifdef _WIN32
    g_mem = VirtualAlloc(NULL, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc(g_mem + 0x10000u, WORDS * 4, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(g_mem + 0xFD800000u, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    HANDLE t = CreateThread(NULL, 0, w_consumer, NULL, 0, NULL);
#else
    g_mem = mmap(NULL, 0x100000000ull, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    pthread_t t; pthread_create(&t, NULL, consumer, NULL);
#endif
    for (uint32_t round = 1; round <= ROUNDS; round++) {
        produce(&c, g_mem, round);
        while (__atomic_load_n((uint32_t *)(g_mem + 0xFD800044u), __ATOMIC_ACQUIRE) != round) { }
    }
#ifdef _WIN32
    WaitForSingleObject(t, INFINITE);
#else
    pthread_join(t, NULL);
#endif
    printf(g_bad ? "FAIL %d rounds saw stale pushbuffer words\n" : "OK\n", g_bad);
    return g_bad != 0;
}
"""


def _cc():
    # the asm checks read x86-64 AT&T output; other hosts run the producer/consumer test only
    return shutil.which("clang")


def _x86_64():
    import platform
    return platform.machine().lower() in ("amd64", "x86_64")


def _asm(src, defines):
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "w.c"
        s = Path(tmp) / "w.s"
        c.write_text(src, encoding="utf-8")
        r = subprocess.run([_cc(), "-O2", "-w", "-S", *defines,
                            "-I", str(ROOT / "templates" / "runtime"), str(c), "-o", str(s)],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr[-3000:]
        text = s.read_text()
    body = re.search(r"^writer:(.*?)-- End function", text, re.S | re.M).group(1)
    return [l.strip() for l in body.splitlines() if re.match(r"^\t[a-z]", l)]


def test_fence_keeps_a_pushbuffer_store_in_front_of_the_kickoff():
    if not _cc() or not _x86_64():
        pytest.skip("clang on an x86-64 host needed")
    fenced = _asm(WRITER, ["-DKICKOFF_PTR(a)=RECOMP_LIB_GPTR(a)"])
    stores = [i for i in fenced if i.startswith("movl") and ("$286331153" in i or "$572662306" in i or "$4100" in i)]
    order = ["first" if "$286331153" in i else "put" if "$4100" in i else "second" for i in stores]
    assert order == ["first", "put", "second"], fenced


def test_negative_control_without_the_fence_the_first_store_is_dropped():
    if not _cc() or not _x86_64():
        pytest.skip("clang on an x86-64 host needed")
    plain = _asm(WRITER, ["-DKICKOFF_PTR(a)=XBOX_PTR(a)"])
    assert not any("$286331153" in i for i in plain), \
        "expected clang to drop the overwritten store without the fence: " + repr(plain)


def test_producer_consumer_through_the_kickoff_path():
    if not _cc():
        pytest.skip("clang not on PATH")
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / "pc.c"
        exe = Path(tmp) / "pc.exe"
        c.write_text(PRODUCER, encoding="utf-8")
        import os
        extra = [] if os.name == "nt" else ["-lpthread"]
        r = subprocess.run([_cc(), "-O2", "-w", "-I", str(ROOT / "templates" / "runtime"), str(c), "-o", str(exe), *extra],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr[-3000:]
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
        assert ran.returncode == 0 and "OK" in ran.stdout, ran.stdout + ran.stderr
