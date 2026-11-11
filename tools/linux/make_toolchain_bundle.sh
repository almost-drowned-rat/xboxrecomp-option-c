#!/usr/bin/env bash
# make_toolchain_bundle.sh - the compiler + Python a Linux release ships, from the official upstream packages
# (maintainer, 2026-11-10: the player downloads nothing; the launcher's toolchain.txt format 2 points here).
#
#   bash tools/linux/make_toolchain_bundle.sh \
#       --llvm     <LLVM-23.1.1-Linux-X64.tar.zst> \
#       --python   <cpython-3.12.x+...-x86_64-unknown-linux-gnu-install_only.tar.gz> \
#       --debs     <folder with the Ubuntu 22.04 .debs: libc6 libc6-dev linux-libc-dev libgcc-11-dev libgcc-s1 libstdc++6> \
#       --capstone <site-packages folder holding capstone/ + capstone-*.dist-info> \
#       --vulkan-headers <include folder holding vulkan/vulkan_core.h> \
#       --out      <empty or missing folder> \
#       [--cxx-headers]   libc++'s headers too (a developer bundle that builds glslang: build_glslang.sh)
#       [--llvm-license <LICENSE.TXT>]  the LLVM project's licence text (the official Linux tarball has none;
#                         llvm-mingw of the same LLVM release carries it)
#
# Writes <out>/llvm (clang + lld + clang's resource headers + compiler-rt builtins/crt + sysroot/) and
# <out>/python (python-build-standalone + capstone, the bulk the lifter never imports removed), then a smoke test
# (a C program with threads, math and dlopen, linked against a Vulkan call, built with the bundle only) and a size
# report. Nothing is downloaded and nothing outside <out> is written (a scratch folder under it is removed again).
#
# The sysroot is glibc 2.35 (Ubuntu 22.04): what is linked against it runs on every distribution with glibc >= 2.35
# (the smoke test checks that no symbol asks for more). libvulkan.so in the sysroot is a LINK STUB (every vk* the
# headers declare, SONAME libvulkan.so.1): the game finds the real loader of the system at run time - the GPU
# driver belongs to the system, the launcher checks for it before it starts the game.
#
# The official LLVM build is big (clang 269 MB, lld 204 MB, all targets): fine for test packages; a slim
# X86-only clang (-O2/-O3 + LTO, never -Os; the first start must not get slower) is a later, separate step.
# Needs: bash, cmake (its libarchive reads .tar.zst - WSL has no zstd tool), dpkg-deb, tar, the host's linker
# (`ld`, once: the bundle's clang links lld's ICU shim with it before lld can run), objdump/readelf for the checks.
set -euo pipefail

LLVM_TAR= PY_TAR= DEBS= CAPSTONE= VKH= OUT= CXX_HEADERS=0 LLVM_LICENSE=
while [ $# -gt 0 ]; do
    case "$1" in
        --llvm-license) LLVM_LICENSE="$2"; shift 2 ;;
        --cxx-headers) CXX_HEADERS=1; shift ;;
        --llvm) LLVM_TAR="$2"; shift 2 ;;
        --python) PY_TAR="$2"; shift 2 ;;
        --debs) DEBS="$2"; shift 2 ;;
        --capstone) CAPSTONE="$2"; shift 2 ;;
        --vulkan-headers) VKH="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
for v in LLVM_TAR PY_TAR DEBS CAPSTONE VKH OUT; do
    [ -n "${!v}" ] || { echo "missing --${v,,} (see the header)" >&2; exit 2; }
done
[ -f "$VKH/vulkan/vulkan_core.h" ] || { echo "no vulkan/vulkan_core.h under $VKH" >&2; exit 2; }
if [ -e "$OUT" ] && [ -n "$(ls -A "$OUT")" ]; then
    echo "$OUT is not empty - give a new folder" >&2
    exit 2
fi
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
TMP="$OUT/.scratch"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

# ── LLVM: only what compiles and links C for x86-64 Linux ───────────────────
echo "== llvm"
(cd "$TMP" && cmake -E tar tf "$LLVM_TAR") > "$TMP/llvm_list.txt"    # once, to a file: no SIGPIPE under pipefail
TOP="$(sed -n '1s|/.*||p' "$TMP/llvm_list.txt")"
VER_MAJOR="$(grep -oE '/lib/clang/[0-9]+/' "$TMP/llvm_list.txt" | sed -n '1s|[^0-9]||gp')"
RT="lib/clang/$VER_MAJOR"
(cd "$TMP" && cmake -E tar xf "$LLVM_TAR" \
    "$TOP/bin/clang-$VER_MAJOR" "$TOP/bin/clang" "$TOP/bin/lld" "$TOP/bin/ld.lld" "$TOP/bin/llvm-objcopy" \
    "$TOP/$RT/include" "$TOP/$RT/lib/x86_64-unknown-linux-gnu" "$TOP/lib/x86_64-unknown-linux-gnu" \
    "$TOP/include/c++" "$TOP/include/x86_64-unknown-linux-gnu" \
    "$TOP/LICENSE.TXT" 2>/dev/null) || (cd "$TMP" && cmake -E tar xf "$LLVM_TAR" \
    "$TOP/bin/clang-$VER_MAJOR" "$TOP/bin/clang" "$TOP/bin/lld" "$TOP/bin/ld.lld" "$TOP/bin/llvm-objcopy" \
    "$TOP/$RT/include" "$TOP/$RT/lib/x86_64-unknown-linux-gnu" "$TOP/lib/x86_64-unknown-linux-gnu" \
    "$TOP/include/c++" "$TOP/include/x86_64-unknown-linux-gnu")
L="$OUT/llvm"
mkdir -p "$L/bin" "$L/$RT/lib/x86_64-unknown-linux-gnu"
cp -a "$TMP/$TOP/bin/clang-$VER_MAJOR" "$L/bin/"
ln -sf "clang-$VER_MAJOR" "$L/bin/clang"
ln -sf "clang-$VER_MAJOR" "$L/bin/clang++"        # C++ mode, only for building the prebuilt libraries (glslang)
cp -a "$TMP/$TOP/bin/lld" "$L/bin/"
ln -sf lld "$L/bin/ld.lld"
# libc++ / libc++abi / libunwind, STATIC only: the C++ parts of the game (glslang) are linked into it, so the game
# needs no C++ runtime from the player's system and asks for no GLIBCXX version (libstdc++ from GCC 13 would).
mkdir -p "$L/lib/x86_64-unknown-linux-gnu"
for a in libc++.a libc++abi.a libunwind.a; do
    cp "$TMP/$TOP/lib/x86_64-unknown-linux-gnu/$a" "$L/lib/x86_64-unknown-linux-gnu/"
done
if [ "$CXX_HEADERS" = 1 ]; then              # --cxx-headers: for building glslang & co (a developer bundle)
    mkdir -p "$L/include"
    cp -a "$TMP/$TOP/include/c++" "$L/include/"
    cp -a "$TMP/$TOP/include/x86_64-unknown-linux-gnu" "$L/include/"
fi
cp -a "$TMP/$TOP/$RT/include" "$L/$RT/"
# compiler-rt: the builtins and crtbegin/end only (sanitizers, profiles, fuzzers are not for a player)
for f in libclang_rt.builtins.a clang_rt.crtbegin.o clang_rt.crtend.o; do
    [ -f "$TMP/$TOP/$RT/lib/x86_64-unknown-linux-gnu/$f" ] && cp -a "$TMP/$TOP/$RT/lib/x86_64-unknown-linux-gnu/$f" \
        "$L/$RT/lib/x86_64-unknown-linux-gnu/"
done
# resource headers for other targets' intrinsics are not needed on x86-64 (arm/riscv/ppc/hexagon/...)
find "$L/$RT/include" -maxdepth 1 \( -name 'arm*' -o -name 'riscv*' -o -name 'hexagon*' -o -name 'hvx*' \
    -o -name 'altivec.h' -o -name 'htm*' -o -name 'vecintrin.h' -o -name 'velintrin*' -o -name 'msa.h' \
    -o -name 'lasx*' -o -name 'lsx*' -o -name 'larchintrin.h' -o -name 'wasm*' -o -name 'sifive*' \
    -o -name 'ppc_wrappers' -o -name 'cuda_wrappers' -o -name 'openmp_wrappers' -o -name 'llvm_libc_wrappers' \
    -o -name 'zos_wrappers' -o -name '__clang_cuda*' -o -name '__clang_hip*' -o -name 'opencl*' \
    -o -name 'hlsl*' -o -name 'spirv*' -o -name 'gpuintrin*' -o -name 'nvptx*' -o -name 'amdgpu*' \) \
    -exec rm -rf {} +
[ -f "$TMP/$TOP/LICENSE.TXT" ] && cp "$TMP/$TOP/LICENSE.TXT" "$L/LICENSE.TXT"
OBJCOPY="$TMP/$TOP/bin/llvm-objcopy"       # from the tarball, only for this script (removed with the scratch)
"$OBJCOPY" --strip-unneeded "$L/bin/clang-$VER_MAJOR"
"$OBJCOPY" --strip-unneeded "$L/bin/lld"
echo "clang $("$L/bin/clang" --version | sed -n 1p)"

# ── sysroot: glibc 2.35 from the Ubuntu 22.04 packages ──────────────────────
echo "== sysroot"
S="$L/sysroot"
mkdir -p "$TMP/debs"
for p in libc6 libc6-dev linux-libc-dev libgcc-11-dev libgcc-s1 libstdc++6; do   # libstdc++6: for clang/lld only
    deb="$(ls "$DEBS"/${p}_*_amd64.deb 2>/dev/null | head -1)"
    [ -n "$deb" ] || { echo "no $p deb in $DEBS" >&2; exit 2; }
    dpkg-deb -x "$deb" "$TMP/debs"
    echo "  $(basename "$deb")"
done
mkdir -p "$S"
for d in usr/include lib/x86_64-linux-gnu lib64 usr/lib/x86_64-linux-gnu usr/lib/gcc/x86_64-linux-gnu; do
    [ -e "$TMP/debs/$d" ] && { mkdir -p "$S/$(dirname "$d")"; cp -a "$TMP/debs/$d" "$S/$d"; }
done
# what a C link never needs: the dynamic libs' helper programs and locales, audit/gconv modules, static
# variants of the libs we link dynamically stay (libc_nonshared.a, crt*.o are needed)
rm -rf "$S/usr/lib/x86_64-linux-gnu/gconv" "$S/usr/lib/x86_64-linux-gnu/audit" "$S/usr/lib/x86_64-linux-gnu/perl"*
rm -rf "$S/usr/lib/gcc/x86_64-linux-gnu/11/plugin" "$S/usr/lib/gcc/x86_64-linux-gnu/11/include-fixed"
# static glibc (libc.a, libm.a, ...): we link dynamically. Kept: the *_nonshared.a parts the .so linker scripts
# name (libc_nonshared.a), the empty stubs glibc 2.34+ keeps for -lpthread / -ldl / -lrt / -lutil / -lanl (the
# code moved into libc.so.6), and libgcc.a / libgcc_eh.a from GCC (another folder)
find "$S/usr/lib/x86_64-linux-gnu" -maxdepth 1 -name '*.a' ! -name '*_nonshared.a' \
    ! -name libpthread.a ! -name libdl.a ! -name librt.a ! -name libutil.a ! -name libanl.a -delete
# absolute symlinks inside the debs (libm.so -> /lib/x86_64-linux-gnu/libm.so.6) point at the HOST: make them
# relative inside the sysroot
while IFS= read -r -d '' ln; do
    t="$(readlink "$ln")"
    case "$t" in
        /*) rel="$(realpath -m --relative-to="$(dirname "$ln")" "$S$t")"; ln -sfn "$rel" "$ln" ;;
    esac
done < <(find "$S" -type l -print0)
# ── what the official clang / lld load at run time (their RUNPATH is $ORIGIN/../lib) ────────────────
# clang + lld want libstdc++.so.6 (GLIBCXX_3.4.30) and libgcc_s.so.1: the Ubuntu 22.04 copies (GCC 12, runtime
# library exception) go next to them, so the player's C++ runtime does not matter. lld also links ICU 70 (through
# its static libxml2, BIND_NOW): no current distribution has ICU 70. libxml2 uses ICU only to convert character
# sets of XML input - lld reads XML only for Windows (COFF) manifests, never for an ELF link - so a shim answers
# ucnv_open with an error (libxml2 then reports the encoding as unsupported) and defines the other 6 functions.
# libz.so.1 and liblzma.so.5 (XZ_5.0) are on every desktop distribution and stay the system's.
echo "== llvm run-time libraries"
mkdir -p "$L/lib"
for so in libstdc++.so.6 libgcc_s.so.1; do
    src="$(find "$TMP/debs" -name "$so" | sort | sed -n 1p)"          # the SONAME link (or file) itself
    [ -n "$src" ] || { echo "no $so in the debs" >&2; exit 2; }
    cp -L "$src" "$L/lib/$so"                                             # the real library behind the link
    echo "  $so <- $(basename "$(readlink -f "$src")")"
done
cat > "$TMP/icu_shim.c" <<'EOF'
/* generated by make_toolchain_bundle.sh: the 7 ICU 70 functions lld's static libxml2 binds (BIND_NOW).
 * No conversion is offered: ucnv_open fails, so libxml2 treats the encoding as unsupported. ELF links never
 * reach this code (lld reads XML only for COFF manifests). */
typedef int UErrorCode;
#define U_UNSUPPORTED_ERROR 16
void *ucnv_open_70(const char *name, UErrorCode *err) { (void)name; if (err) *err = U_UNSUPPORTED_ERROR; return 0; }
void ucnv_close_70(void *cnv) { (void)cnv; }
void ucnv_setToUCallBack_70(void *c, void *f, const void *x, void *of, const void **ox, UErrorCode *err)
{ (void)c; (void)f; (void)x; (void)of; (void)ox; if (err) *err = U_UNSUPPORTED_ERROR; }
void ucnv_setFromUCallBack_70(void *c, void *f, const void *x, void *of, const void **ox, UErrorCode *err)
{ (void)c; (void)f; (void)x; (void)of; (void)ox; if (err) *err = U_UNSUPPORTED_ERROR; }
void ucnv_convertEx_70(void *t, void *s, char **tg, const char *tl, const char **sr, const char *sl, void *ps,
                       void **psrc, void **ptgt, const void *pl, char reset, char flush, UErrorCode *err)
{ (void)t; (void)s; (void)tg; (void)tl; (void)sr; (void)sl; (void)ps; (void)psrc; (void)ptgt; (void)pl;
  (void)reset; (void)flush; if (err) *err = U_UNSUPPORTED_ERROR; }
void UCNV_TO_U_CALLBACK_STOP_70(void) {}
void UCNV_FROM_U_CALLBACK_STOP_70(void) {}
EOF
# lld cannot link its own shim before the shim exists: the bundle's clang with the HOST linker (bootstrap)
"$L/bin/clang" -shared -fPIC -O2 -nostdlib -Wl,-soname,libicuuc.so.70 -o "$L/lib/libicuuc.so.70" "$TMP/icu_shim.c"
for n in libicui18n libicudata; do               # NEEDED, but nothing is bound from them: empty libraries
    echo "/* generated: an empty $n.so.70 (see libicuuc.so.70) */ int ${n}_shim_70;" > "$TMP/$n.c"
    "$L/bin/clang" -shared -fPIC -nostdlib -Wl,-soname,$n.so.70 -o "$L/lib/$n.so.70" "$TMP/$n.c"
done
echo "  $("$L/bin/ld.lld" --version | sed -n 1p)"

# the Vulkan link stub (see the header)
grep -oE 'VKAPI_ATTR [^;]*VKAPI_CALL vk[A-Za-z0-9_]+\(' "$VKH/vulkan/vulkan_core.h" \
    | grep -oE 'vk[A-Za-z0-9_]+\($' | tr -d '(' | sort -u > "$TMP/vk_names.txt"
{ echo "/* generated by make_toolchain_bundle.sh: a link stub, never loaded (SONAME libvulkan.so.1) */"
  while read -r n; do echo "void $n(void) {}"; done < "$TMP/vk_names.txt"; } > "$TMP/vk_stub.c"
"$L/bin/clang" --sysroot="$S" -fuse-ld=lld -shared -fPIC -O0 -nostdlib -Wl,-soname,libvulkan.so.1 \
    -o "$S/usr/lib/x86_64-linux-gnu/libvulkan.so" "$TMP/vk_stub.c"
echo "  vulkan stub: $(wc -l < "$TMP/vk_names.txt") functions"

# ── Python + capstone ────────────────────────────────────────────────────────
echo "== python"
P="$OUT/python"
(cd "$TMP" && tar xzf "$PY_TAR")
mv "$TMP/python" "$P"
PYV="$(ls -d "$P"/lib/python3.* | head -1)"
SITE="$PYV/site-packages"
cp -a "$CAPSTONE/capstone" "$SITE/"
cp -a "$CAPSTONE"/capstone-*.dist-info "$SITE/"
# what the lifter never imports: tests, GUI, IDLE, docs, the C headers / static lib (nothing is built against it)
# _dbm (gdbm/ndbm) and _crypt (libcrypt): separate files, the lifter imports neither - gone
rm -f "$PYV"/lib-dynload/_dbm.* "$PYV"/lib-dynload/_crypt.*
rm -rf "$PYV/test" "$PYV/idlelib" "$PYV/tkinter" "$PYV/turtledemo" "$PYV/ensurepip" "$PYV/lib2to3" \
       "$PYV/pydoc_data" "$PYV/turtle.py" "$PYV/config-"* "$P/include" "$P/share" \
       "$PYV"/lib-dynload/_tkinter* "$P"/lib/libtcl* "$P"/lib/libtk* "$P"/lib/tcl* "$P"/lib/tk* "$P"/lib/itcl* \
       "$P"/lib/thread* "$P"/lib/pkgconfig "$P"/lib/libpython*.a
find "$P" -name __pycache__ -type d -prune -exec rm -rf {} +
find "$P" -path '*/site-packages/pip*' -prune -exec rm -rf {} + 2>/dev/null || true
# the interpreter is linked statically (no NEEDED libpython): the 114 MB shared libpython is for embedding only;
# the helper scripts are for developers
if ! readelf -d "$P/bin/python3.12" | grep -q 'NEEDED.*libpython'; then
    rm -f "$P"/lib/libpython3*.so*
fi
rm -f "$P"/bin/2to3* "$P"/bin/idle3* "$P"/bin/pip* "$P"/bin/pydoc3* "$P"/bin/python3*-config
# debug info out (python-build-standalone ships it): --strip-unneeded keeps .dynsym, which the extension modules
# resolve the interpreter's functions through
"$OBJCOPY" --strip-unneeded "$P/bin/python3.12"
find "$P" -name '*.so' -type f -exec "$OBJCOPY" --strip-unneeded {} \;
"$P/bin/python3" -I -c "import capstone, sys; print('python', sys.version.split()[0], 'capstone', capstone.__version__)"
mkdir -p "$P/LICENSES"
cp "$PYV/LICENSE.txt" "$P/LICENSES/Python.txt"                      # PSF licence (kept in the stdlib folder)
cp "$SITE"/capstone-*.dist-info/LICENSE* "$P/LICENSES/capstone.txt"  # BSD-3-Clause
# The interpreter has OpenSSL, SQLite, zlib, bzip2, xz, libffi, expat, mpdecimal, ncurses, libedit and libuuid
# linked INTO it (built-in modules; they cannot be left out without building CPython). Their licence texts are not
# in the install_only archive: until the maintainer decides on python-build-standalone's full archive (which has
# them), the build host's Debian/Ubuntu copyright files of the same upstream projects stand in, provenance noted.
# Library versions as the interpreter reports them:
VERS="$("$P/bin/python3" -I -c "import ssl, sqlite3, zlib, pyexpat, _decimal; print('OpenSSL %s; SQLite %s; zlib %s; %s; mpdecimal %s' % (ssl.OPENSSL_VERSION.split()[1], sqlite3.sqlite_version, zlib.ZLIB_RUNTIME_VERSION, pyexpat.EXPAT_VERSION, _decimal.__libmpdec_version__))")"
MISSING=""
for pair in OpenSSL:libssl3t64,libssl3 SQLite:libsqlite3-0 zlib:zlib1g bzip2:libbz2-1.0 xz:liblzma5 libffi:libffi8 \
            expat:libexpat1 ncurses:libncursesw6,libtinfo6 libedit:libedit2 libuuid:libuuid1 mpdecimal:libmpdec3,libmpdec4; do
    lib="${pair%%:*}" found=""
    IFS=, read -ra cands <<< "${pair#*:}"
    for pkg in "${cands[@]}"; do
        if [ -f "/usr/share/doc/$pkg/copyright" ]; then
            { echo "# $lib, linked into bin/python3.12 by python-build-standalone ($VERS)."
              echo "# Licence text: the build host's /usr/share/doc/$pkg/copyright (Debian/Ubuntu packaging of the same"
              echo "# upstream project; the upstream licence is the part that applies to the copy in the interpreter)."
              echo; cat "/usr/share/doc/$pkg/copyright"; } > "$P/LICENSES/$lib.txt"
            found=1
            break
        fi
    done
    [ -n "$found" ] || MISSING="$MISSING $lib"
done
{ echo "Python: python-build-standalone ($(basename "$PY_TAR")), unchanged except unused parts removed and"
  echo "  debug info stripped - Python.txt (PSF). capstone 5 (Python bindings + libcapstone.so) - capstone.txt."
  echo "Linked into the interpreter: $VERS; bzip2, xz, libffi, ncurses, libedit, libuuid."
  echo "  Their licence texts: <name>.txt, taken from the build host's distribution packages (see each file's head)."
  [ -z "$MISSING" ] || echo "  OPEN - no licence text available offline yet:$MISSING (python-build-standalone's full archive has it)."
} > "$P/LICENSES/NOTICE.txt"
[ -z "$MISSING" ] || echo "  WARNING: Python licence texts still missing:$MISSING" >&2
# precompiled once here: the player's first start reads .pyc (python -I -B writes none in the read-only mount)
"$P/bin/python3" -I -m compileall -q -j 0 "$PYV" >/dev/null

# ── licences: llvm/LICENSES and python/LICENSES (the release script takes the folders whole) ────────
echo "== licences"
LIC="$L/LICENSES"
mkdir -p "$LIC"
# the official Linux tarball carries no LICENSE.TXT (only a short notice in include/llvm/Support): the LLVM
# project's licence text comes from --llvm-license (the same release's LICENSE.TXT, e.g. llvm-mingw's)
if [ -f "$TMP/$TOP/LICENSE.TXT" ]; then
    cp "$TMP/$TOP/LICENSE.TXT" "$LIC/LLVM.txt"
elif [ -n "$LLVM_LICENSE" ] && grep -q 'LLVM Exceptions' "$LLVM_LICENSE"; then
    cp "$LLVM_LICENSE" "$LIC/LLVM.txt"       # clang, lld, compiler-rt, libc++, libc++abi, libunwind
else
    echo "the LLVM tarball has no LICENSE.TXT: give --llvm-license <LLVM project LICENSE.TXT>" >&2
    exit 2
fi
rm -f "$L/LICENSE.TXT"
for p in libc6 libc6-dev linux-libc-dev libgcc-11-dev libgcc-s1 libstdc++6; do
    c="$TMP/debs/usr/share/doc/$p/copyright"
    if [ ! -f "$c" ]; then                    # Ubuntu links some doc folders to the compiler's base package
        c="$(find "$TMP/debs/usr/share/doc" -path "*/${p%%[0-9]*}*/copyright" -type f | sed -n 1p)"
    fi
    if [ -z "$c" ] || [ ! -f "$c" ]; then
        case "$p" in libgcc*|libstdc++*)
            # Ubuntu links the GCC runtime packages' doc folder to gcc-<n>-base (not among the debs): the same
            # GPL-3 + GCC Runtime Library Exception 3.1 text from the build host's gcc-*-base, provenance noted
            g="$(ls /usr/share/doc/gcc-*-base/copyright 2>/dev/null | sort -V | tail -1)"
            if [ -n "$g" ]; then
                { echo "# $p (Ubuntu 22.04) ships no copyright file of its own (its doc folder links to gcc-11/12-base)."
                  echo "# The GCC runtime licence text below is the build host's $g"
                  echo "# (GPL-3 with the GCC Runtime Library Exception 3.1, the same terms for GCC 11 and 12)."
                  echo; cat "$g"; } > "$LIC/$p.copyright.txt"
                continue
            fi ;;
        esac
    fi
    if [ -n "$c" ] && [ -f "$c" ]; then
        cp "$c" "$LIC/$p.copyright.txt"
    else
        echo "  WARNING: no copyright file for $p in the debs" >&2
        echo "$p: no copyright file in the package; see https://packages.ubuntu.com/jammy/$p" > "$LIC/$p.copyright.txt"
    fi
done
cat > "$LIC/NOTICE.txt" <<EOF
Compiler bundle of an xboxrecomp port release (tools/linux/make_toolchain_bundle.sh).

bin/clang, bin/lld, lib/clang, lib/x86_64-unknown-linux-gnu (libc++, libc++abi, libunwind): LLVM $VER_MAJOR,
  the official release LLVM-*-Linux-X64.tar.zst, unchanged except stripped - LLVM.txt (Apache-2.0 with LLVM
  exceptions).
sysroot/: GNU C library 2.35, Linux kernel headers, GCC 11 startup files / libgcc from Ubuntu 22.04 packages -
  libc6*.copyright.txt (LGPL-2.1+), linux-libc-dev.copyright.txt (GPL-2 headers, with the syscall note),
  libgcc-11-dev.copyright.txt (GPL-3 with the GCC Runtime Library Exception).
lib/libstdc++.so.6, lib/libgcc_s.so.1: the GCC 12 runtime from Ubuntu 22.04 (libstdc++6, libgcc-s1), loaded by
  clang and lld only - libstdc++6.copyright.txt / libgcc-s1.copyright.txt (GPL-3 with the GCC Runtime Library
  Exception).
lib/libicuuc.so.70, libicui18n.so.70, libicudata.so.70: NOT ICU. Small libraries generated by this script (the
  toolkit's own code, no ICU code or data): they define the seven ICU 70 function names lld's built-in libxml2
  binds, and answer every call with an error. lld never converts XML character sets for an ELF link.
sysroot/usr/lib/x86_64-linux-gnu/libvulkan.so: NOT the Vulkan loader. A link stub generated by this script from
  the function names in the Vulkan headers (vulkan_core.h, Apache-2.0 OR MIT); it is never loaded - the game uses
  the Vulkan loader of the player's system.
EOF
ls "$LIC"

# ── smoke test: the bundle alone builds a program that runs, needs glibc <= 2.35 and links Vulkan ──────
echo "== smoke"
cat > "$TMP/smoke.c" <<'EOF'
#define _GNU_SOURCE                         /* RTLD_DEFAULT */
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
void vkGetInstanceProcAddr(void);
static void *run(void *a) { return a; }
int main(int argc, char **argv) {
    pthread_t t; void *r = NULL; char buf[32];
    if (argc > 1 && !strcmp(argv[1], "vk")) vkGetInstanceProcAddr();   /* linked, never called */
    pthread_create(&t, NULL, run, buf); pthread_join(t, &r);
    snprintf(buf, sizeof buf, "%.3f", sqrt(2.0));
    printf("smoke ok %s %s %s\n", buf, r == buf ? "thread" : "NO-THREAD", dlsym(RTLD_DEFAULT, "printf") ? "dl" : "NO-DL");
    return 0;
}
EOF
"$L/bin/clang" --sysroot="$S" -fuse-ld=lld -O2 -o "$TMP/smoke" "$TMP/smoke.c" -lm -lpthread -ldl -lvulkan
"$TMP/smoke"
max="$(objdump -T "$TMP/smoke" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -uV | tail -1)"
echo "  highest glibc symbol version: $max"
case "$max" in
    GLIBC_2.[0-9]|GLIBC_2.[12][0-9]|GLIBC_2.3[0-5]) ;;
    *) echo "  FAIL: needs more than glibc 2.35" >&2; exit 1 ;;
esac
readelf -d "$TMP/smoke" | grep -q 'NEEDED.*libvulkan.so.1' || { echo "  FAIL: no NEEDED libvulkan.so.1" >&2; exit 1; }
echo "  NEEDED libvulkan.so.1: yes; interpreter $(readelf -l "$TMP/smoke" | grep -oE '/lib64/[^]]*')"

# ── sizes ────────────────────────────────────────────────────────────────────
echo "== sizes (MB)"
du -sm "$L/bin" "$L/$RT" "$S" "$L" "$P" "$OUT" | sed "s|$OUT/||"
(cd "$OUT" && cmake -E tar cf "$TMP/estimate.tar.zst" --zstd llvm python)
echo "  zstd (cmake default level) of llvm + python: $(( $(stat -c %s "$TMP/estimate.tar.zst") / 1048576 )) MB"
echo "done: $OUT"
