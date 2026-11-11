"""prune_llvm_mingw.py - the llvm-mingw a Windows release ships, cut to what builds a port's game on x86-64.

    py -3 -I tools/release/prune_llvm_mingw.py <llvm-mingw-...-ucrt-x86_64.zip> <out.zip> [--smoke]

Maintainer, 2026-11-10 (Orta v1.1.0, THPS 2X's next release on the same model): the player downloads nothing,
the compiler ships in the release (launcher toolchain.txt format 2, tools/bundle/<zip>), so its size counts.
Kept: the C/C++ compiler and linker for x86_64-w64-mingw32 (clang.exe, clang++, ld.lld, llvm-rc and every DLL
they import, found from their PE import tables), the target config files, clang's resource headers and the
x86_64 builtins, the shared mingw-w64 headers and the x86_64 sysroot. Dropped: the aarch64 / armv7 / i686 /
arm64ec sysroots, lldb, clangd, clang-tidy, clang-format and the other tools, sanitizer / fuzzer / profile
runtimes, the bundled Python (the release ships its own), busybox, docs.

The zip's single top folder is kept as it is (toolchain.txt's bundle line names it). The output is written with
deflate level 9 - the player's first start unpacks it once (unpack time: tools/release timing, compare to the
unpruned zip). --smoke unpacks the result into a temporary folder and builds a C program with a resource file
with it, then runs it: the gate that nothing needed was dropped (the real gate is a port's first-start E2E).
Nothing is downloaded; the input zip is only read.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

KEEP_BIN_PROGRAMS = ["clang.exe", "clang++.exe", "clang-23.exe", "ld.lld.exe", "lld.exe", "lld-link.exe",
                     "llvm-rc.exe", "llvm-windres.exe", "llvm-ar.exe", "llvm-ranlib.exe"]
TARGET = "x86_64-w64-mingw32"


def top_folder(names):
    tops = {n.split("/", 1)[0] for n in names if n}
    if len(tops) != 1:
        sys.exit("expected one top folder in the zip, found %s" % sorted(tops)[:5])
    return tops.pop()


def keep(rel, bin_keep):
    """rel: the path below the top folder (forward slashes)."""
    parts = rel.split("/")
    first = parts[0]
    if first == "bin":
        name = parts[-1]
        return name in bin_keep or name.endswith(".cfg")        # target config files (tiny)
    if first == "include":
        return True                                             # mingw-w64 headers shared by every target + libc++
    if first == TARGET:
        return True                                             # the x86_64 sysroot (libs, import libs, DLLs)
    if first == "lib":
        # lib/clang/<v>/include (resource headers) and lib/clang/<v>/lib/windows/libclang_rt.builtins-x86_64.a
        m = re.match(r"lib/clang/\d+/(.*)", rel)
        if not m:
            return False                                        # libscanbuild, libear, cmake files
        sub = m.group(1)
        if sub.startswith("include/"):
            return True
        return bool(re.match(r"lib/windows/libclang_rt\.builtins-x86_64\.a$", sub))
    return first in ("LICENSE.TXT", "README.md")                # licences stay with the binaries


def pe_imports(readobj, exe):
    out = subprocess.run([readobj, "--coff-imports", exe], capture_output=True, text=True).stdout
    return set(m.lower() for m in re.findall(r"Name:\s+(\S+\.dll)", out, flags=re.I))


def closure(src_bin, readobj):
    """the kept programs plus every DLL of bin/ they import, transitively"""
    have = {n.lower(): n for n in os.listdir(src_bin)}
    want = [p for p in KEEP_BIN_PROGRAMS if p.lower() in have]
    done = set()
    while want:
        n = want.pop()
        if n.lower() in done:
            continue
        done.add(n.lower())
        for dll in pe_imports(readobj, os.path.join(src_bin, have[n.lower()])):
            if dll in have and dll not in done:
                want.append(have[dll])
    return {have[n] for n in done}


def smoke(out_zip):
    tmp = tempfile.mkdtemp(prefix="prune_smoke_")
    try:
        with zipfile.ZipFile(out_zip) as z:
            z.extractall(tmp)
        top = os.path.join(tmp, os.listdir(tmp)[0])
        clang = os.path.join(top, "bin", "clang.exe")
        rc = os.path.join(top, "bin", "llvm-rc.exe")
        c = os.path.join(tmp, "t.c")
        with open(c, "w") as f:
            f.write('#include <windows.h>\n#include <math.h>\n#include <stdio.h>\n'
                    'int main(void){ HANDLE e = CreateEventW(NULL, TRUE, FALSE, NULL);\n'
                    ' printf("smoke ok %.3f %s\\n", sqrt(2.0), e ? "win32" : "NO-WIN32"); CloseHandle(e); return 0; }\n')
        with open(os.path.join(tmp, "t.rc"), "w") as f:
            f.write('1 VERSIONINFO\nFILEVERSION 1,0,0,0\nBEGIN\nEND\n')
        subprocess.run([rc, "/fo", os.path.join(tmp, "t.res"), os.path.join(tmp, "t.rc")], check=True)
        subprocess.run([clang, "-O2", "-o", os.path.join(tmp, "t.exe"), c, os.path.join(tmp, "t.res"),
                        "-fuse-ld=lld"], check=True)
        r = subprocess.run([os.path.join(tmp, "t.exe")], capture_output=True, text=True, check=True)
        print("  " + r.stdout.strip())
        if "smoke ok" not in r.stdout or "NO-" in r.stdout:
            sys.exit("smoke FAILED")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    src_zip, out_zip, do_smoke = argv[1], argv[2], "--smoke" in argv[3:]
    if os.path.exists(out_zip):
        sys.exit("%s exists - give a new name" % out_zip)
    with zipfile.ZipFile(src_zip) as z:
        names = z.namelist()
        top = top_folder(names)
        # the import tables come from the zip's own binaries: unpack bin/ once into a scratch folder
        scratch = tempfile.mkdtemp(prefix="prune_bin_")
        try:
            for n in names:
                if n.startswith(top + "/bin/") and not n.endswith("/"):
                    z.extract(n, scratch)
            src_bin = os.path.join(scratch, top, "bin")
            bin_keep = closure(src_bin, os.path.join(src_bin, "llvm-readobj.exe"))
        finally:
            shutil.rmtree(scratch, ignore_errors=True)
        print("bin kept: " + ", ".join(sorted(bin_keep)))
        kept = dropped = 0
        size_in = size_out = 0
        with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as o:
            for info in z.infolist():
                size_in += info.file_size
                rel = info.filename[len(top) + 1:] if info.filename.startswith(top + "/") else ""
                if rel and not info.filename.endswith("/") and keep(rel, bin_keep):
                    o.writestr(info, z.read(info), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
                    kept += 1
                    size_out += info.file_size
                elif not info.filename.endswith("/"):
                    dropped += 1
    mb = 1024 * 1024
    print("files kept %d, dropped %d; unpacked %d -> %d MB; zip %d -> %d MB"
          % (kept, dropped, size_in // mb, size_out // mb, os.path.getsize(src_zip) // mb,
             os.path.getsize(out_zip) // mb))
    if do_smoke:
        print("smoke:")
        smoke(out_zip)
    print("done: " + out_zip)


if __name__ == "__main__":
    main(sys.argv)
