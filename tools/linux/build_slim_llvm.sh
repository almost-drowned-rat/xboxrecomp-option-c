#!/usr/bin/env bash
# build_slim_llvm.sh - the slim clang + lld for a Linux RELEASE bundle (maintainer, 2026-11-12: the AppImage of
# v1.1.0 is the slim one, target 50-80 MB): ONE multicall binary `llvm` (LLVM_TOOL_LLVM_DRIVER_BUILD - clang and
# lld share the LLVM libraries instead of carrying one copy each), X86 target only, Release (-O3) + ThinLTO, never
# -Os (the player's first start compiles the game with it and must not get slower). Built with the developer
# bundle's own clang 23 against its glibc 2.35 sysroot (cmake/linux-sysroot.cmake), libc++ / libc++abi / libunwind
# linked statically; no zlib / zstd / libxml2 / terminfo, so the binary needs nothing but glibc (no ICU shim, no
# libstdc++ copy next to it).
#
#   bash tools/linux/build_slim_llvm.sh --src <llvm-project-23.1.1.src> --bundle <developer bundle (--cxx-headers)> \
#        --out <empty or missing folder> --ar <llvm-ar of LLVM 23, llvm-ranlib next to it>
#        [--lto thin|off] [--profdata <file.profdata>] [--instrument]
#
# --instrument builds a PGO-instrumented compiler (profile: compile the game's generated C with it, then
# llvm-profdata merge), --profdata uses such a profile. PGO IS REQUIRED for a release (2026-11-12, Orta's 8 biggest
# generated files, -O2, 4 jobs: official LLVM 29.3 s, slim O3+ThinLTO 42.0 s, slim O3+ThinLTO+PGO 29.2 s):
#   1. --instrument --lto off --out <i>   (the developer bundle needs libclang_rt.profile.a from the official tarball)
#   2. LLVM_PROFILE_FILE=<p>/%p-%m.profraw <i>/bin/clang -resource-dir <bundle>/llvm/lib/clang/<major> -O2 -c every
#      generated .c of the game (the launcher's flags), plus one <i>/bin/clang -fuse-ld=lld link of the objects
#   3. llvm-profdata merge -o clang.profdata <p>/*.profraw   (llvm-profdata of the same release)
#   4. --lto thin --profdata clang.profdata --out <final>
# --ar: the official tarball's llvm-ar (build host only). Writes <out>/bin/llvm + the links clang, clang-<major>,
# clang++, lld, ld.lld (what make_toolchain_bundle.sh --slim-bin takes), <out>/build.log. JOBS (default 4): compile
# and ThinLTO link jobs.
set -euo pipefail

SRC= BUNDLE= OUT= LTO=thin PROFDATA= INSTR=0 JOBS="${JOBS:-4}" LLVM_AR=
while [ $# -gt 0 ]; do
    case "$1" in
        --src) SRC="$2"; shift 2 ;;
        --bundle) BUNDLE="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --lto) LTO="$2"; shift 2 ;;
        --profdata) PROFDATA="$2"; shift 2 ;;
        --instrument) INSTR=1; shift ;;
        --ar) LLVM_AR="$2"; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$SRC" ] && [ -n "$BUNDLE" ] && [ -n "$OUT" ] && [ -n "$LLVM_AR" ] || { echo "see the header for the arguments" >&2; exit 2; }
# the archives hold LLVM bitcode (LTO): the host's GNU ar / ranlib cannot index it - llvm-ar of the same release
LLVM_RANLIB="$(dirname "$LLVM_AR")/llvm-ranlib"
[ -x "$LLVM_AR" ] && [ -x "$LLVM_RANLIB" ] || { echo "--ar: llvm-ar with llvm-ranlib next to it" >&2; exit 2; }
[ -f "$SRC/llvm/CMakeLists.txt" ] || { echo "no llvm/CMakeLists.txt under $SRC" >&2; exit 2; }
BUNDLE="$(cd "$BUNDLE" && pwd)"
[ -d "$BUNDLE/llvm/include/c++/v1" ] || { echo "$BUNDLE has no libc++ headers: make it with --cxx-headers" >&2; exit 2; }
if [ -e "$OUT" ] && [ -n "$(ls -A "$OUT")" ]; then echo "$OUT is not empty - give a new folder" >&2; exit 2; fi
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
SRC="$(cd "$SRC" && pwd)"
TK="$(cd "$(dirname "$0")/../.." && pwd)"
B="$OUT/.build"
LOG="$OUT/build.log"

CXXLIB="$BUNDLE/llvm/lib/x86_64-unknown-linux-gnu"
LDFLAGS_ALL="-fuse-ld=lld -L$CXXLIB"           # = linux-sysroot.cmake's _INIT (a -D here replaces the _INIT)
EXTRA=()
case "$LTO" in
    thin) EXTRA+=(-DLLVM_ENABLE_LTO=Thin -DLLVM_PARALLEL_LINK_JOBS=1)
          LDFLAGS_ALL="$LDFLAGS_ALL -Wl,--thinlto-jobs=$JOBS" ;;
    off) ;;
    *) echo "--lto thin|off" >&2; exit 2 ;;
esac
# the multicall binary takes every tool that can join it: only clang and lld are wanted
for t in LLVM_AR LLVM_CXXFILT LLVM_DWP LLVM_LIPO LLVM_MT LLVM_NM LLVM_OBJCOPY LLVM_OBJDUMP LLVM_RC LLVM_READOBJ \
         LLVM_SIZE LLVM_SYMBOLIZER DSYMUTIL LLVM_ML LLVM_LIBTOOL_DARWIN LLVM_IFS LLVM_GSYMUTIL LLVM_DWARFUTIL \
         LLVM_STRINGS LLVM_READTAPI LLVM_DEBUGINFOD LLVM_DEBUGINFOD_FIND LLVM_PROFDATA LLVM_CONFIG LLVM_LTO \
         LLVM_LTO2 LLVM_CGDATA LLVM_DIS LLVM_AS LLVM_LINK LLVM_EXTRACT LLVM_MCA LLVM_COV LLVM_EXEGESIS LLVM_JITLINK \
         LLVM_RTDYLD LLVM_REDUCE LLVM_SPLIT LLVM_TLI_CHECKER LLVM_DWARFDUMP LLVM_PDBUTIL LLVM_RESHAPE \
         LLVM_REMARKUTIL LLVM_ML_BUILD LLC OPT LLI BUGPOINT; do
    EXTRA+=("-DLLVM_TOOL_${t}_BUILD=OFF")
done
for t in CLANG_SCAN_DEPS CLANG_FORMAT CLANG_REPL CLANG_LINKER_WRAPPER CLANG_NVLINK_WRAPPER CLANG_OFFLOAD_BUNDLER \
         CLANG_OFFLOAD_PACKAGER CLANG_OFFLOAD_WRAPPER CLANG_INSTALLAPI CLANG_SYCL_LINKER CLANG_EXTDEF_MAPPING \
         CLANG_IMPORT_TEST CLANG_DIFF CLANG_FUZZER CLANG_REFACTOR CLANG_RENAME C_INDEX_TEST LIBCLANG AMDGPU_ARCH \
         NVPTX_ARCH CLANG_CHECK DIAGTOOL APINOTES_TEST CLANG_SHLIB CLANG_FORMAT_VS; do
    EXTRA+=("-DCLANG_TOOL_${t}_BUILD=OFF")
done
[ "$INSTR" = 1 ] && EXTRA+=(-DLLVM_BUILD_INSTRUMENTED=IR -DLLVM_BUILD_RUNTIME=OFF)
[ -n "$PROFDATA" ] && EXTRA+=("-DLLVM_PROFDATA_FILE=$(cd "$(dirname "$PROFDATA")" && pwd)/$(basename "$PROFDATA")")

echo "== configure (lto $LTO, instrument $INSTR, profdata ${PROFDATA:-none}, jobs $JOBS)"
cmake -S "$SRC/llvm" -B "$B" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TK/cmake/linux-sysroot.cmake" -DXR_LINUX_BUNDLE="$BUNDLE" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_PROJECTS="clang;lld" \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DLLVM_TOOL_LLVM_DRIVER_BUILD=ON \
    -DLLVM_DISTRIBUTION_COMPONENTS="clang;lld;llvm-driver" \
    -DCMAKE_AR="$LLVM_AR" -DCMAKE_RANLIB="$LLVM_RANLIB" \
    -DLLVM_STATIC_LINK_CXX_STDLIB=ON \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
    -DLLVM_ENABLE_LIBCXX=ON \
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF \
    -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_LIBPFM=OFF -DLLVM_ENABLE_CURL=OFF -DLLVM_ENABLE_HTTPLIB=OFF \
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_DOCS=OFF \
    -DCLANG_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_DOCS=OFF \
    -DCLANG_ENABLE_ARCMT=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_PLUGIN_SUPPORT=OFF \
    -DLLVM_ENABLE_BINDINGS=OFF -DLLVM_ENABLE_PLUGINS=OFF -DLLVM_ENABLE_ASSERTIONS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS_ALL" -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS_ALL" \
    -DCMAKE_CXX_STANDARD_LIBRARIES="$CXXLIB/libc++abi.a $CXXLIB/libunwind.a -lpthread -ldl" \
    "${EXTRA[@]}" >"$LOG" 2>&1 || { tail -40 "$LOG"; exit 1; }

echo "== build llvm-driver"
start=$(date +%s)
cmake --build "$B" --target llvm-driver -j "$JOBS" >>"$LOG" 2>&1 || { tail -40 "$LOG"; exit 1; }
echo "   $(( $(date +%s) - start )) s"

mkdir -p "$OUT/bin"
cp "$B/bin/llvm" "$OUT/bin/llvm"
MAJOR="$("$OUT/bin/llvm" clang --version | sed -n '1s/.*version \([0-9]*\).*/\1/p')"
for n in clang "clang-$MAJOR" clang++ lld ld.lld; do ln -sf llvm "$OUT/bin/$n"; done
rm -rf "$B"
echo "== result"
ls -la "$OUT/bin"
"$OUT/bin/clang" --version | head -1
"$OUT/bin/ld.lld" --version
echo "   NEEDED: $(readelf -d "$OUT/bin/llvm" | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/' | tr '\n' ' ')"
echo "   max GLIBC: $(objdump -T "$OUT/bin/llvm" | grep -oE 'GLIBC_[0-9.]+' | sort -t. -k2 -n -u | tail -1)"
