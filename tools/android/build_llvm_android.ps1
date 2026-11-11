# build_llvm_android.ps1 - clang + lld for android-arm64 (the ON-DEVICE compiler), cross built on Windows with the NDK.
# Two stages: (1) the host tablegen tools (llvm-min-tblgen, llvm-tblgen, clang-tblgen) native with the clang on PATH
# (llvm-mingw), (2) clang + lld for aarch64-linux-android: AArch64 target only, static LLVM (no libLLVM.so),
# c++_static, PIE, flexible page sizes. Then make_device_toolchain.ps1 strips them and adds the sysroot slice.
#
#   powershell -ExecutionPolicy Bypass -File tools\android\build_llvm_android.ps1 -LlvmSrc <llvm-project source>
#       -Ndk <android-ndk-rXX> -Work <build folder> [-Api 29] [-Jobs 4]
#
# The source needs llvm, clang, lld, cmake, libunwind, third-party, libc (LLVM 23: FindLibcCommonUtils) and the
# docs folders (add_subdirectory'd unconditionally); tests are not needed.
# Output: <Work>\host\bin\*tblgen.exe, <Work>\android\bin\clang + ld.lld (unstripped, ~1.4 GB / 0.85 GB with the
# NDK's debug info), <Work>\android\lib\clang\<N>\include. Ninja resumes a stopped stage.
# First run 2026-11-11 (LLVM 23.1.1, NDK r30, -j4, this laptop): stage 1 94 s, stage 2 1175 s; stripped clang 84 MB,
# ld.lld 56 MB.
param(
    [Parameter(Mandatory = $true)][string]$LlvmSrc,
    [Parameter(Mandatory = $true)][string]$Ndk,
    [Parameter(Mandatory = $true)][string]$Work,
    [int]$Api = 29,
    [int]$Jobs = 4
)
# Continue, not Stop: Windows PowerShell 5.1 turns a native tool's stderr line under *> into a terminating
# NativeCommandError (cmake warnings); every native step checks $LASTEXITCODE.
$ErrorActionPreference = "Continue"
function Cm($p) { return ($p -replace '\\', '/') }
$Src = Join-Path $LlvmSrc "llvm"
foreach ($d in @("llvm", "clang", "lld", "cmake", "libc", "llvm\docs", "clang\docs")) {
    if (-not (Test-Path (Join-Path $LlvmSrc $d))) { throw "source incomplete: $d missing in $LlvmSrc" }
}
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$HostB = Join-Path $Work "host"
$AndB = Join-Path $Work "android"
$Log = Join-Path $Work "build.log"
$t = [Diagnostics.Stopwatch]::StartNew()

# 1. host tablegen
$clang = (Get-Command clang -ErrorAction SilentlyContinue).Source
if (-not $clang) { throw "a host clang (llvm-mingw) must be on PATH for the tablegen tools" }
$hb = Split-Path -Parent $clang
if (-not (Test-Path (Join-Path $HostB "bin\clang-tblgen.exe"))) {
    Write-Host "== host tablegen"
    & cmake -S $Src -B $HostB -G Ninja -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_C_COMPILER=$(Cm $clang)" "-DCMAKE_CXX_COMPILER=$(Cm (Join-Path $hb 'clang++.exe'))" `
        "-DCMAKE_RC_COMPILER=$(Cm (Join-Path $hb 'llvm-rc.exe'))" `
        -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=AArch64 -DLLVM_INCLUDE_TESTS=OFF `
        -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF `
        -DLLVM_ENABLE_LIBXML2=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_ARCMT=OFF *> $Log
    if ($LASTEXITCODE -ne 0) { Get-Content $Log -Tail 20; throw "host configure failed (log $Log)" }
    & cmake --build $HostB -j $Jobs --target llvm-min-tblgen llvm-tblgen clang-tblgen *>> $Log
    if ($LASTEXITCODE -ne 0) { Get-Content $Log -Tail 30; throw "host tablegen build failed (log $Log)" }
    Write-Host ("   done ({0:N0} s)" -f $t.Elapsed.TotalSeconds)
}
$HostBin = Join-Path $HostB "bin"

# 2. clang + lld for android-arm64
Write-Host "== clang + lld for aarch64-linux-android$Api"
if (-not (Test-Path (Join-Path $AndB "build.ninja"))) {
    & cmake -S $Src -B $AndB -G Ninja -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_TOOLCHAIN_FILE=$(Cm (Join-Path $Ndk 'build\cmake\android.toolchain.cmake'))" `
        -DANDROID_ABI=arm64-v8a "-DANDROID_PLATFORM=android-$Api" -DANDROID_STL=c++_static `
        -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON `
        "-DLLVM_ENABLE_PROJECTS=clang;lld" -DLLVM_TARGETS_TO_BUILD=AArch64 `
        -DLLVM_HOST_TRIPLE=aarch64-linux-android "-DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-android$Api" `
        "-DLLVM_NATIVE_TOOL_DIR=$(Cm $HostBin)" `
        "-DLLVM_TABLEGEN=$(Cm (Join-Path $HostBin 'llvm-tblgen.exe'))" `
        "-DCLANG_TABLEGEN=$(Cm (Join-Path $HostBin 'clang-tblgen.exe'))" `
        -DBUILD_SHARED_LIBS=OFF -DLLVM_BUILD_LLVM_DYLIB=OFF -DLLVM_LINK_LLVM_DYLIB=OFF `
        -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_DOCS=OFF `
        -DLLVM_INCLUDE_UTILS=OFF -DLLVM_BUILD_UTILS=OFF `
        -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF `
        -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_LIBPFM=OFF -DLLVM_ENABLE_BINDINGS=OFF `
        -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_ARCMT=OFF -DCLANG_PLUGIN_SUPPORT=OFF `
        -DCLANG_BUILD_TOOLS=OFF -DCLANG_INCLUDE_DOCS=OFF -DCLANG_INCLUDE_TESTS=OFF `
        -DLLVM_ENABLE_ASSERTIONS=OFF *>> $Log
    if ($LASTEXITCODE -ne 0) { Get-Content $Log -Tail 30; throw "android configure failed (log $Log)" }
}
& cmake --build $AndB -j $Jobs --target clang lld clang-resource-headers *>> $Log
if ($LASTEXITCODE -ne 0) { Get-Content $Log -Tail 40; throw "android build failed (log $Log)" }
Write-Host ("clang + lld built ({0:N0} s total): {1}" -f $t.Elapsed.TotalSeconds, (Join-Path $AndB "bin"))
