"""lgpl_source.py - the corresponding source of the toolkit's LGPL parts for a release (licence option C,
maintainer 2026-11-17).

    python lgpl_source.py <toolkit dir> <out dir> [--version <release version>]

Copies, keeping the toolkit's paths under <out dir>:
  - the whole APU module (src/apu: the xemu MCPX APU core, LGPL-2.1-or-later, with this project's changes, and
    the MIT files built into the same library) and src/nv2a_gl/gl_vsh_ff.c (xemu, LGPL-2.1-or-later),
  - every toolkit header those include (transitively), and the cmake helpers the module's CMakeLists uses,
  - LICENSES/LGPL-2.1.txt, NOTICE, THIRD_PARTY_NOTICES.txt,
and writes SOURCE.txt: the toolkit commit, the file list, how to rebuild the library and relink the game.
Fails (exit 1) when a quoted include cannot be found - the bundle must be complete. Standard library only."""
import argparse
import os
import re
import shutil
import subprocess
import sys

SEED_DIRS = ["src/apu"]
SEED_FILES = ["src/nv2a_gl/gl_vsh_ff.c", "LICENSES/LGPL-2.1.txt", "NOTICE", "THIRD_PARTY_NOTICES.txt"]
# the include directories the module's targets see (src/nv2a: where qemu_shim.h lived before 2026-11-16)
INC_ROOTS = ["src", "src/platform", "src/kernel", "src/nv2a"]
INC_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
CMAKE_RE = re.compile(r'include\(\$\{CMAKE_CURRENT_LIST_DIR\}/([^)]+)\)')
SOURCE_EXT = (".c", ".h", ".inc", ".cpp")
GENERATED = {"xr_build_info.h"}                # written by the build (platform/CMakeLists.txt), not a source


def rel(tk, p):
    return os.path.relpath(p, tk).replace("\\", "/")


def resolve(tk, including, name):
    cands = [os.path.join(os.path.dirname(including), name)] + [os.path.join(tk, r, name) for r in INC_ROOTS]
    for c in cands:
        if os.path.isfile(c):
            return os.path.normpath(c)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("toolkit")
    ap.add_argument("out")
    ap.add_argument("--version", default="")
    ap.add_argument("--seed", action="append", default=[],
                    help="a further LGPL directory or file (toolkit-relative), e.g. src/nv2a for an older tree")
    ap.add_argument("--commit", help="the toolkit line of SOURCE.txt, when <toolkit> is an exported tree without git "
                                     "(a release: the PUBLIC snapshot, e.g. 'xboxrecomp-option-c, tag v1.2.0, commit <sha>')")
    ap.add_argument("--notices", help="THIRD_PARTY_NOTICES.txt to use when the tree has none (older releases)")
    a = ap.parse_args()
    tk = os.path.abspath(a.toolkit)
    files, todo, missing = set(), [], []
    seeds = SEED_FILES[:]
    if a.notices:
        seeds.remove("THIRD_PARTY_NOTICES.txt")
    for s in a.seed:
        if os.path.isdir(os.path.join(tk, s)):
            SEED_DIRS.append(s)
        else:
            seeds.append(s)
    for d in SEED_DIRS:
        for n in sorted(os.listdir(os.path.join(tk, d))):
            p = os.path.join(tk, d, n)
            if os.path.isfile(p):
                todo.append(os.path.normpath(p))
    todo += [os.path.normpath(os.path.join(tk, f)) for f in seeds]
    while todo:
        p = todo.pop()
        if p in files:
            continue
        if not os.path.isfile(p):
            missing.append(rel(tk, p))
            continue
        files.add(p)
        if p.endswith(SOURCE_EXT):
            with open(p, encoding="utf-8", errors="replace") as f:
                for name in INC_RE.findall(f.read()):
                    if name in GENERATED:
                        continue
                    r = resolve(tk, p, name)
                    if r is None:
                        missing.append(f"{rel(tk, p)}: #include \"{name}\"")
                    elif r not in files:
                        todo.append(r)
        elif os.path.basename(p) == "CMakeLists.txt" or p.endswith(".cmake"):
            with open(p, encoding="utf-8", errors="replace") as f:
                for name in CMAKE_RE.findall(f.read()):
                    todo.append(os.path.normpath(os.path.join(os.path.dirname(p), name)))
    if missing:
        print("lgpl_source: not found - the bundle would be incomplete:\n  " + "\n  ".join(sorted(set(missing))))
        return 1
    if os.path.isdir(a.out):
        shutil.rmtree(a.out)
    for p in sorted(files):
        dst = os.path.join(a.out, rel(tk, p))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(p, dst)
    if a.notices:
        shutil.copyfile(a.notices, os.path.join(a.out, "THIRD_PARTY_NOTICES.txt"))
    commit, dirty = a.commit or "unknown (no git)", ""
    if not a.commit:
        try:
            commit = subprocess.run(["git", "-C", tk, "rev-parse", "HEAD"], capture_output=True, text=True,
                                    check=True).stdout.strip()
            dirty = subprocess.run(["git", "-C", tk, "status", "--porcelain", "--",
                                    *sorted(rel(tk, p) for p in files)], capture_output=True, text=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            pass
    names = sorted(rel(tk, p) for p in files)
    incs = [r for r in INC_ROOTS + ["src/apu"] if any(n.startswith(r + "/") for n in names)]
    if "src/apu/apu_module.c" in names:          # the APU module (toolkit since 2026-11-17): replace the library
        how = ("Use a modified library: the APU emulation is a shared library of its own, the APU module\n"
               "(src/apu/apu_module.h). Build it - configure the toolkit with CMake (its top-level CMakeLists.txt)\n"
               "and build the target xbox_apu_module - and put the result in place of the shipped one next to the\n"
               "game: xbox_apu.dll (Windows) / libxbox_apu.so (Linux, Android), or point RECOMP_APU_MODULE=<file>\n"
               "at it. The game's log names the module it loaded (\"[APU] module ...\").")
    else:                                         # older trees: the static library, relinked by the launcher
        how = ("Rebuild the library: configure the toolkit (CMake, the toolkit's top-level CMakeLists.txt) and build\n"
               "the target xbox_apu; or compile the src/apu .c files with " + ", ".join(incs) + " on the include\n"
               "path. Relink the game with a modified library: the release's sdk/lib holds the library the game is\n"
               "linked from on the player's machine (Windows: tools\\sdk\\lib\\libxbox_apu.a; Linux: sdk/lib inside\n"
               "the AppImage - run it once with --appimage-extract and start the extracted AppRun); replace it and\n"
               "run the launcher's repair (--repair), which links the game again.")
    with open(os.path.join(a.out, "SOURCE.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(f"""Corresponding source of the LGPL-2.1-or-later parts in this release{(' (' + a.version + ')') if a.version else ''}
=======================================================================

Toolkit: {commit if a.commit else 'xboxrecomp (Option C), commit ' + commit}{' (with local changes to these files)' if dirty else ''}

The MCPX APU core in src/apu (apu_core.c, apu_vp.c, apu_dsp.c and their headers) and src/nv2a_gl/gl_vsh_ff.c are
extracted from xemu (https://github.com/xemu-project/xemu) and are under the GNU Lesser General Public License,
version 2.1 or (at your option) any later version (LICENSES/LGPL-2.1.txt), with this project's changes. The other
files of src/apu are MIT (THIRD_PARTY_NOTICES.txt); they are built into the same library and are here so that the
library builds from this folder. The toolkit headers listed below are the ones those files include.

{how}

Files ({len(names)}):
""")
        for n in names:
            f.write(f"  {n}\n")
    print(f"lgpl_source: {len(names)} files -> {a.out} (toolkit {commit[:12]}{', dirty' if dirty else ''})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
