#!/usr/bin/env bash
# build_glslang.sh - glslang for a Linux RELEASE: built with the shipped toolchain (make_toolchain_bundle.sh
# --cxx-headers) against its glibc 2.35 sysroot and LLVM's libc++, so the game links it with no C++ runtime from
# the player's system and no GLIBCXX symbol versions (the Ubuntu 24.04 packages are GCC 13 / libstdc++).
#
#   bash tools/linux/build_glslang.sh --src <glslang source> --bundle <bundle out> --prefix <install folder>
#
# As on Windows (the Windows deps: libglslang.a, libglslang-default-resource-limits.a,
# libSPIRV.a, no SPIRV-Tools): ENABLE_OPT=OFF - nv2a_vk uses glslang's C interface without the optimizer
# (src/nv2a_vk/CMakeLists.txt), so SPIRV-Tools is neither built nor needed. Static libraries only, no tools, no
# tests. Then a link test: a C program calling the C interface, linked the way the game is (the C driver, libc++
# and libc++abi as static archives), must run, ask for no libstdc++/libc++ .so and nothing above GLIBC_2.35.
set -euo pipefail

SRC= BUNDLE= PREFIX= JOBS="${JOBS:-4}"
while [ $# -gt 0 ]; do
    case "$1" in
        --src) SRC="$2"; shift 2 ;;
        --bundle) BUNDLE="$2"; shift 2 ;;
        --prefix) PREFIX="$2"; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$SRC" ] && [ -n "$BUNDLE" ] && [ -n "$PREFIX" ] || { echo "see the header for the arguments" >&2; exit 2; }
L="$(cd "$BUNDLE/llvm" && pwd)"
S="$L/sysroot"
CXXLIB="$L/lib/x86_64-unknown-linux-gnu"
[ -d "$L/include/c++/v1" ] || { echo "$BUNDLE has no libc++ headers: make it with --cxx-headers" >&2; exit 2; }
mkdir -p "$PREFIX"
PREFIX="$(cd "$PREFIX" && pwd)"
B="$PREFIX/.build"
rm -rf "$B"

# the toolkit's release toolchain file (cmake/linux-sysroot.cmake): this build is also its test
TK="$(cd "$(dirname "$0")/../.." && pwd)"
cmake -S "$SRC" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TK/cmake/linux-sysroot.cmake" -DXR_LINUX_BUNDLE="$(cd "$BUNDLE" && pwd)" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_SHARED_LIBS=OFF \
    -DENABLE_OPT=OFF -DENABLE_GLSLANG_BINARIES=OFF -DGLSLANG_TESTS=OFF -DENABLE_HLSL=OFF \
    -DGLSLANG_ENABLE_INSTALL=ON -DENABLE_SPVREMAPPER=OFF >"$PREFIX/configure.log" 2>&1 \
    || { tail -30 "$PREFIX/configure.log"; exit 1; }
cmake --build "$B" -j "$JOBS" >"$PREFIX/build.log" 2>&1 || { tail -30 "$PREFIX/build.log"; exit 1; }
cmake --install "$B" >"$PREFIX/install.log" 2>&1 || { tail -30 "$PREFIX/install.log"; exit 1; }
rm -rf "$B"
echo "== installed"
ls "$PREFIX/lib"

echo "== link test (the C driver, as the game links)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
cat > "$T/t.c" <<'EOF'
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <stdio.h>
int main(void) {
    const char *src = "#version 450\nlayout(location=0) out vec4 o;\nvoid main(){ o = vec4(1.0); }\n";
    glslang_input_t in = {0};
    glslang_shader_t *sh;
    int ok;
    glslang_initialize_process();
    in.language = GLSLANG_SOURCE_GLSL; in.stage = GLSLANG_STAGE_FRAGMENT; in.client = GLSLANG_CLIENT_VULKAN;
    in.client_version = GLSLANG_TARGET_VULKAN_1_3; in.target_language = GLSLANG_TARGET_SPV;
    in.target_language_version = GLSLANG_TARGET_SPV_1_6; in.code = src; in.default_version = 450;
    in.default_profile = GLSLANG_NO_PROFILE; in.messages = GLSLANG_MSG_DEFAULT_BIT;
    in.resource = glslang_default_resource();
    sh = glslang_shader_create(&in);
    ok = glslang_shader_preprocess(sh, &in) && glslang_shader_parse(sh, &in);
    printf("glslang %s\n", ok ? "compiles a shader" : glslang_shader_get_info_log(sh));
    glslang_shader_delete(sh);
    glslang_finalize_process();
    return ok ? 0 : 1;
}
EOF
"$L/bin/clang" --sysroot="$S" -O2 -I"$PREFIX/include" -c "$T/t.c" -o "$T/t.o"
"$L/bin/clang" --sysroot="$S" -fuse-ld=lld -o "$T/t" "$T/t.o" -L"$PREFIX/lib" \
    -lglslang -lglslang-default-resource-limits -lSPIRV -lOSDependent -lMachineIndependent -lGenericCodeGen \
    "$CXXLIB/libc++.a" "$CXXLIB/libc++abi.a" "$CXXLIB/libunwind.a" -lpthread -lm 2>"$T/link.log" || {
    # the library set differs between glslang versions: link what is installed, in a group
    "$L/bin/clang" --sysroot="$S" -fuse-ld=lld -o "$T/t" "$T/t.o" -Wl,--start-group "$PREFIX"/lib/*.a \
        "$CXXLIB/libc++.a" "$CXXLIB/libc++abi.a" "$CXXLIB/libunwind.a" -Wl,--end-group -lpthread -lm; }
"$T/t"
if readelf -d "$T/t" | grep -E 'NEEDED.*(libstdc\+\+|libc\+\+|libgcc_s)'; then
    echo "FAIL: a C++ runtime from the system is still needed" >&2; exit 1
fi
max="$(objdump -T "$T/t" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -uV | tail -1)"
echo "  NEEDED: $(readelf -d "$T/t" | grep -oE 'Shared library: \[[^]]+\]' | tr '\n' ' ')"
echo "  highest glibc symbol version: $max"
case "$max" in
    GLIBC_2.[0-9]|GLIBC_2.[12][0-9]|GLIBC_2.3[0-5]) ;;
    *) echo "FAIL: needs more than glibc 2.35" >&2; exit 1 ;;
esac
echo "  link line for the game (C driver): ... -L<prefix>/lib <the libs above> {TOOLS}/llvm/lib/x86_64-unknown-linux-gnu/libc++.a libc++abi.a libunwind.a -lpthread -lm"
echo "done: $PREFIX"
