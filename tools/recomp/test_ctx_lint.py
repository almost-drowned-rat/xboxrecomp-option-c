"""Option C (C.1): the guest register set is passed explicitly, never found through
a global or thread-local.

Generated code takes X86Ctx *ctx (restrict from C.1-5): the compiler may then keep
register values in host registers across calls, so any path that reaches the set
some other way -- a global instance, a thread-local pointer, a per-thread lookup --
reads or writes a stale copy. This lint keeps that from coming back:

1. No global or thread-local X86Ctx (or X86Ctx *) and no register globals
   (g_eax ... defined as variables) in src/ or templates/.
2. The register names are macros over the ctx in scope, in both headers.
3. The one exception, the diagnostic pointer (xbox_diag_ctx: crash handler,
   watchdog, heap watch, state snapshot), is read only in diagnostic code --
   never in a file whose functions guest code can reach while it runs.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [p for d in ("src", "templates") for p in (ROOT / d).rglob("*")
           if p.suffix in (".c", ".h", ".cpp") and p.is_file()]
REGS = r"(?:eax|ecx|edx|ebx|esp|ebp|esi|edi|seh_ebp|fs_base|eflags_sys|call_flags|df|fp_stack|fp_top|fp_cmp|fp_control_word|fp_cc|mxcsr|xmm[0-7]|mm[0-7])"
# Files whose code runs as, or on behalf of, guest code: no diagnostic lookups there.
GUEST_REACHABLE = {"kernel_bridge.c", "kernel_hal.c", "kernel_rtl.c", "recomp_trace.c", "xdk_dsound.c",
                   "xdk_xinput.c", "apu_core.c", "ohci.c", "recomp_types.h"}
DIAG_ALLOWED = {"xbox_memory_layout.c", "xbox_memory_layout.h", "main.c", "host_win32.c"}


def _code(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def test_no_global_or_thread_local_register_set():
    bad = []
    for p in SOURCES:
        code = _code(p.read_text(encoding="utf-8", errors="replace"))
        for m in re.finditer(r"(?m)^[^\n#(]*\b(?:RECOMP_TLS|__thread|_Thread_local|__declspec\(thread\)|XBOX_THREAD_LOCAL)\b[^\n;]*\bX86Ctx\b[^\n;]*;", code):
            if "t_diag_ctx" not in m.group(0):
                bad.append(f"{p.relative_to(ROOT)}: {m.group(0).strip()}")
        for m in re.finditer(r"(?m)^\s*(?:extern\s+)?(?:static\s+)?(?:RECOMP_TLS\s+)?X86Ctx\s+g_\w+", code):
            bad.append(f"{p.relative_to(ROOT)}: {m.group(0).strip()}")
        for m in re.finditer(rf"(?m)^\s*(?:extern\s+)?(?:static\s+)?(?:\w+_TLS\s+|RECOMP_TLS\s+)?(?:uint32_t|uint16_t|int|double|RecompXmm|RecompMmx)\s+g_{REGS}\b", code):
            bad.append(f"{p.relative_to(ROOT)}: {m.group(0).strip()}")
    assert not bad, "global/thread-local register state:\n" + "\n".join(bad)


def test_register_names_are_ctx_fields_in_both_headers():
    for h in ("templates/runtime/recomp_types.h", "src/kernel/xbox_memory_layout.h"):
        text = (ROOT / h).read_text(encoding="utf-8")
        assert "#define g_eax             (ctx->r_eax)" in text, h
        assert "#define g_fp_stack        (ctx->fp_stack)" in text, h
        assert "#define RECOMP_CALL_GUEST(fn)  ((fn)(ctx, RECOMP_GUEST_BASE))" in text, h
        assert "g_x86" not in _code(text), h


def test_diagnostic_pointer_only_in_diagnostic_code():
    bad = []
    for p in SOURCES:
        code = _code(p.read_text(encoding="utf-8", errors="replace"))
        if "xbox_diag_ctx(" not in code and "t_diag_ctx" not in code:
            continue
        if p.name in GUEST_REACHABLE or p.name not in DIAG_ALLOWED:
            bad.append(str(p.relative_to(ROOT)))
    assert not bad, "xbox_diag_ctx() used outside diagnostic code: " + ", ".join(bad)


def test_runtime_paths_take_ctx_from_their_caller():
    # the C.1-3/C.1-4 transition markers are gone
    bad = [str(p.relative_to(ROOT)) for p in SOURCES
           if "&g_x86" in p.read_text(encoding="utf-8", errors="replace")]
    assert not bad, "still reaching a global register set: " + ", ".join(bad)
