"""hot_path_io.py - lint: no blocking log/file I/O in the hot paths (kernel calls, DPCs, ISRs, the one-CPU gate).

Standing rule (maintainer, 2026-10-27): code in a kernel call, a DPC, an ISR or anything else that holds the
one-CPU gate does no blocking file or log I/O; console output goes through the logger thread (src/kernel/
log_async.h). The hot-path libraries (kernel, USB, APU) get that header force-included by
cmake/HotPathLogging.cmake, so their fprintf/printf/fputs/fwrite/fflush on stderr/stdout are queued. This lint
checks what the redirect cannot:

  1. each hot-path library calls xboxrecomp_hot_path_logging(<target>) in its CMakeLists.txt;
  2. no runtime source outside those libraries uses the gate/ISR/DPC entry points (a new hot file elsewhere);
  3. no hot source undoes or bypasses the redirect (#undef fprintf, (fprintf)(...), __mingw_fprintf, ...) or
     writes to the console another way (OutputDebugString, WriteConsole, _write, WriteFile on a std handle);
  4. every file open in a hot source (fopen, _wfopen, CreateFile ...) is listed in FILE_IO_ALLOWED with the
     reason it is not on a hot path or is the call's own job (a title's NtCreateFile opens a file).

  py -3 tools/hot_path_io.py [<repo root>]     -> findings, exit 1 if any
"""
import os
import re
import sys

HOT_LIBS = {                      # CMakeLists.txt (repo-relative) -> target
    "src/kernel/CMakeLists.txt": "xbox_kernel",
    "src/usb/CMakeLists.txt": "xbox_usb",
    "src/apu/CMakeLists.txt": "xbox_apu",
}

# Calls that only make sense in an ISR/DPC/gate path: a source using them belongs to a hot library.
HOT_API = re.compile(r"\b(xbox_GateTryEnter|xbox_GateTakeEdge|xbox_GateLeave|kernel_run_dpc|xbox_set_irq_line|"
                     r"dpc_queue_insert)\s*\(")

REDIRECTED = ("fprintf", "vfprintf", "printf", "fputs", "puts", "fputc", "putc", "putchar", "fwrite", "fflush")

BYPASS = [
    (re.compile(r"#\s*undef\s+(" + "|".join(REDIRECTED) + r")\b"), "undoes the log_async.h redirect"),
    (re.compile(r"\(\s*(" + "|".join(REDIRECTED) + r")\s*\)\s*\("), "calls around the log_async.h redirect"),
    (re.compile(r"\b(__mingw_v?f?printf|__builtin_v?f?printf|__builtin_fwrite|__builtin_puts|_v?f?printf_[sp]|"
                r"_cprintf|_cputs|fwprintf|wprintf|vfwprintf|_putws|fputws)\s*\("),
     "console output that log_async.h does not queue"),
    (re.compile(r"\b(OutputDebugString[AW]?|WriteConsole[AW]?)\s*\("), "console output that log_async.h does not queue"),
    (re.compile(r"\b_write\s*\(\s*[12]\s*,"), "writes the console handle directly"),
    (re.compile(r"\bWriteFile\s*\(\s*GetStdHandle"), "writes the console handle directly"),
]

FILE_OPEN = re.compile(r"\b(fopen|_wfopen|_fsopen|_wfsopen|freopen|_wfreopen|CreateFile[AW]?|_open|_wopen|"
                       r"xbox_fopen_utf8)\s*\(")

# (source, function) -> why a file open there is allowed. Keep the reason honest: "opt-in diagnostic" is only
# a reason when the diagnostic is off by default; a hot-path open that is on by default must move off the path.
FILE_IO_ALLOWED = {
    ("src/kernel/recomp_trace.c", "prof_dump"): "opt-in profile census (RECOMP_TRACE_PROFILE_DUMP), trace builds only",
    # The title's own file calls: opening the file is the kernel call's job.
    ("src/kernel/kernel_file.c", "xbox_NtCreateFile"): "the title's NtCreateFile/NtOpenFile",
    ("src/kernel/kernel_file.c", "shadow_open"): "the title's own save write (shadow copy of the file it opened)",
    ("src/kernel/kernel_path.c", "xbox_fopen_utf8"): "the helper itself; its callers are checked where they are",
    # Start-up only: the port picks its version before the title runs (no kernel call, DPC or ISR path).
    ("src/kernel/xbox_variant.c", "xbox_variant_file_sha256"): "SHA-256 of default.xbe at start-up (xbox_variant_detect)",
    ("src/kernel/xbox_variant.c", "file_exists"): "default.xbe present? at start-up (xbox_variant_detect)",
    # Start-up, before the title's first instruction.
    ("src/kernel/kernel_path.c", "xbox_write_partition_table"): "start-up: writes the partition table once",
    ("src/kernel/kernel_path.c", "storage_migrate"): "start-up: one-time save-area migration",
    ("src/kernel/kernel_path.c", "xbox_path_init_cfg"): "start-up: creates the storage image once",
    ("src/kernel/kernel_xbox.c", "xbox_identity_load"): "start-up: reads the console identity once",
    ("src/kernel/kernel_thunks.c", "xbox_kernel_init"): "start-up: opens the kernel log file once",
    # At exit.
    ("src/kernel/icall_feedback.c", "dump_sites"): "at exit: writes the indirect-call feedback",
    ("src/kernel/icall_feedback.c", "recomp_icall_feedback_dump"): "at exit: writes the indirect-call feedback",
    # Opt-in diagnostics, off by default, on host threads that hold no gate.
    ("src/kernel/frame_time.c", "start"): "opt-in RECOMP_FRAME_TIME: opened once at the first counted frame, written on the render thread",
    ("src/kernel/nv2a_pb_exec.c", "dump_surface_bmp"): "opt-in RECOMP_FB_DUMP, push-buffer executor thread",
    ("src/kernel/nv2a_pb_exec.c", "dump_texture_bmp"): "opt-in RECOMP_TEX_DUMP, push-buffer executor thread",
    ("src/kernel/xbox_memory_layout.c", "xbox_StateSnapshot"): "opt-in snapshot a project calls explicitly",
    ("src/apu/apu_xaudio2.c", "apu_wav"): "opt-in RECOMP_APU_WAV, audio device thread",
    ("src/apu/apu_dsp.c", "apu_bin_diag"): "opt-in RECOMP_APU_GP_DUMP, once, APU frame thread (no gate)",
}


def strip_c(text):
    """Comments and string/char literals blanked (newlines kept, so line numbers stay)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + "".join(ch if ch == "\n" else " " for ch in text[i + 1:j - 1]) + c)
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


FUNC_HEAD = re.compile(r"([A-Za-z_]\w*)\s*\([^;{}]*\)\s*$", re.S)


def functions(code):
    """[(name, start, end)] of the top-level brace blocks that follow a function head."""
    res, depth, start, name = [], 0, 0, None
    for i, c in enumerate(code):
        if c == "{":
            if depth == 0:
                m = FUNC_HEAD.search(code[max(0, i - 600):i])
                name, start = (m.group(1) if m else None), i
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0 and name:
                res.append((name, start, i))
                name = None
    return res


def function_at(funcs, pos):
    for name, s, e in funcs:
        if s <= pos <= e:
            return name
    return None


def check_source(rel, text, allowed=None):
    """Findings for one hot source: [(rel, line, message)]."""
    allowed = FILE_IO_ALLOWED if allowed is None else allowed
    code = strip_c(text)
    found = []
    line_of = lambda pos: code.count("\n", 0, pos) + 1
    if os.path.basename(rel) != "log_async.c":
        for rx, why in BYPASS:
            for m in rx.finditer(code):
                found.append((rel, line_of(m.start()), "%s: %s" % (m.group(0).strip(), why)))
    funcs = functions(code)
    for m in FILE_OPEN.finditer(code):
        fn = function_at(funcs, m.start())
        if fn is None or fn == m.group(1):
            continue                    # a declaration or the helper's own definition head
        if (rel, fn) not in allowed:
            found.append((rel, line_of(m.start()),
                          "%s in %s(): file I/O in a hot-path source - move it off the path or list it in "
                          "tools/hot_path_io.py FILE_IO_ALLOWED with the reason" % (m.group(1), fn)))
    return found


def hot_sources(root):
    srcs = []
    for cm in HOT_LIBS:
        d = os.path.dirname(cm)
        text = open(os.path.join(root, cm), encoding="utf-8").read()
        srcs += [d + "/" + f for f in re.findall(r"^\s*([\w./]+\.c)\s*$", text, re.M)]
    return sorted(set(srcs))


def check_repo(root):
    found = []
    for cm, target in HOT_LIBS.items():
        lines = [l.split("#", 1)[0] for l in open(os.path.join(root, cm), encoding="utf-8")]
        if not re.search(r"\bxboxrecomp_hot_path_logging\s*\(\s*%s\s*\)" % target, "\n".join(lines)):
            found.append((cm, 0, "does not call xboxrecomp_hot_path_logging(%s)" % target))
    hot = set(hot_sources(root))
    defined = {}
    for dirpath, _, files in os.walk(os.path.join(root, "src")):
        for f in files:
            if not f.endswith((".c", ".cpp")):
                continue
            rel = os.path.relpath(os.path.join(dirpath, f), root).replace("\\", "/")
            text = open(os.path.join(root, rel), encoding="utf-8", errors="replace").read()
            if rel in hot:
                found += check_source(rel, text)
                defined[rel] = {name for name, _, _ in functions(strip_c(text))}
            elif HOT_API.search(strip_c(text)) and os.path.basename(rel) != "log_async.c":
                found.append((rel, 0, "uses the gate/ISR/DPC API but is not in a hot-path library "
                                      "(add it to one, or call xboxrecomp_hot_path_logging for its target and "
                                      "add the CMakeLists.txt to HOT_LIBS)"))
    for rel, fn in FILE_IO_ALLOWED:
        if fn not in defined.get(rel, ()):
            found.append((rel, 0, "FILE_IO_ALLOWED names %s(), which is not a function of this hot source "
                                  "(stale entry)" % fn))
    return found


def main(argv):
    root = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    found = check_repo(root)
    for rel, line, msg in found:
        print("%s:%d: %s" % (rel, line, msg))
    print("hot_path_io: %d finding(s) in %d hot source(s)" % (len(found), len(hot_sources(root))))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
