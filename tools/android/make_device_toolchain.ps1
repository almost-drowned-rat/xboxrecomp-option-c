# make_device_toolchain.ps1 - the ON-DEVICE C toolchain for Android arm64: the clang + ld.lld built for
# android-arm64 (LLVM, cross built with the NDK) plus the slice of the NDK sysroot a C game library needs (API level
# headers, bionic stubs, crt objects) and the compiler runtime. The game library is compiled and linked on the phone
# with it, against the runtime SDK (make_runtime_sdk.ps1). Reusable for every port.
#
#   powershell -ExecutionPolicy Bypass -File tools\android\make_device_toolchain.ps1 -LlvmBuild <llvm build dir>
#       -LlvmSrc <llvm-project source> -Ndk <android-ndk-rXX> -Out <toolchain folder> [-Api 29] [-Test <runtime SDK>]
#
# <llvm build dir>: an LLVM build for the host aarch64-linux-android with bin\clang, bin\ld.lld and lib\clang\<N>\include
# (LLVM_ENABLE_PROJECTS=clang;lld, LLVM_TARGETS_TO_BUILD=AArch64, static, c++_static).
# Out\ (replaced on every run; refused if it exists without toolchain.txt):
#   bin\clang, bin\ld.lld          stripped (ship as lib*.so in nativeLibraryDir - see toolchain.txt for the call)
#   lib\clang\<N>\include          clang's resource headers
#   lib\clang\<N>\lib\linux\...    libclang_rt.builtins-aarch64-android.a, aarch64\libatomic.a, aarch64\libunwind.a
#                                  (from the NDK), also under lib\aarch64-unknown-linux-android<Api> (per-target
#                                  layout, versioned: clang 19+ ignores an unversioned Android directory)
#   sysroot\usr\include            the NDK headers without the other architectures and without libc++
#   sysroot\usr\lib\aarch64-linux-android\<Api>   crt*.o and the shared stubs (no libc++ / libstdc++)
#   LICENSES\                      LLVM (Apache-2.0 WITH LLVM-exception), the NDK's NOTICE
#   toolchain.txt                  versions, sizes and the compile / link calls
# -Test <runtime SDK>: compile + link a minimal game against the SDK with the NDK's clang, but with ONLY this
# folder's sysroot and resource dir (-nostdlibinc paths, --sysroot, -resource-dir) - proves the slice is complete.
param(
    [Parameter(Mandatory = $true)][string]$LlvmBuild,
    [Parameter(Mandatory = $true)][string]$LlvmSrc,
    [Parameter(Mandatory = $true)][string]$Ndk,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$Api = 29,
    [string]$Test = ""
)
# Continue, not Stop: Windows PowerShell 5.1 turns a native tool's stderr line under *> into a terminating
# NativeCommandError; native steps check $LASTEXITCODE.
$ErrorActionPreference = "Continue"
$NdkTc = Join-Path $Ndk "toolchains\llvm\prebuilt\windows-x86_64"
$NdkSys = Join-Path $NdkTc "sysroot"
$Strip = Join-Path $NdkTc "bin\llvm-strip.exe"
foreach ($p in @((Join-Path $LlvmBuild "bin\clang"), (Join-Path $LlvmBuild "bin\ld.lld"), $Strip, $NdkSys)) {
    if (-not (Test-Path $p)) { throw "missing: $p" }
}
$resVer = (Get-ChildItem (Join-Path $LlvmBuild "lib\clang") -Directory | Sort-Object Name | Select-Object -Last 1).Name
$ndkRes = (Get-ChildItem (Join-Path $NdkTc "lib\clang") -Directory | Sort-Object Name | Select-Object -Last 1).FullName

if (Test-Path $Out) {
    if (-not (Test-Path (Join-Path $Out "toolchain.txt"))) { throw "$Out exists and is not a toolchain made by this script - not touched" }
    Remove-Item -Recurse -Force $Out
}
# not "Dir": PowerShell's alias dir (Get-ChildItem) wins over a function of that name
function NewDir($p) {
    New-Item -ItemType Directory -Force -Path $p | Out-Null
    return $p
}
function CopyTree($from, $to, $skipDirs) {
    Get-ChildItem $from -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($from.Length).TrimStart('\')
        $top = ($rel -split '\\')[0]
        if ($skipDirs -contains $top) { return }
        $dst = Join-Path $to $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
        Copy-Item $_.FullName $dst
    }
}

# compiler + linker, stripped
$bin = NewDir (Join-Path $Out "bin")
foreach ($n in @("clang", "ld.lld")) {
    & $Strip --strip-all -o (Join-Path $bin $n) (Join-Path $LlvmBuild "bin\$n")
    if ($LASTEXITCODE -ne 0) { throw "strip $n failed" }
}
# resource dir: headers from this LLVM, runtime from the NDK
$res = NewDir (Join-Path $Out "lib\clang\$resVer")
CopyTree (Join-Path $LlvmBuild "lib\clang\$resVer\include") (NewDir (Join-Path $res "include")) @()
$rtOld = NewDir (Join-Path $res "lib\linux")
# per-target layout with the API level in the name: clang 19+ ignores the unversioned directory for Android
$rtNew = NewDir (Join-Path $res "lib\aarch64-unknown-linux-android$Api")
Copy-Item (Join-Path $ndkRes "lib\linux\libclang_rt.builtins-aarch64-android.a") $rtOld
Copy-Item (Join-Path $ndkRes "lib\linux\libclang_rt.builtins-aarch64-android.a") (Join-Path $rtNew "libclang_rt.builtins.a")
$rtA = NewDir (Join-Path $rtOld "aarch64")
foreach ($n in @("libatomic.a", "libunwind.a")) {
    Copy-Item (Join-Path $ndkRes "lib\linux\aarch64\$n") $rtA
    Copy-Item (Join-Path $ndkRes "lib\linux\aarch64\$n") $rtNew
}
# sysroot slice
$sysOut = NewDir (Join-Path $Out "sysroot\usr")
CopyTree (Join-Path $NdkSys "usr\include") (NewDir (Join-Path $sysOut "include")) @("arm-linux-androideabi", "i686-linux-android",
    "riscv64-linux-android", "x86_64-linux-android", "c++")
$libApi = NewDir (Join-Path $sysOut "lib\aarch64-linux-android\$Api")
Get-ChildItem (Join-Path $NdkSys "usr\lib\aarch64-linux-android\$Api") -File |
    Where-Object { $_.Name -notmatch '^lib(c\+\+|stdc\+\+)' } | ForEach-Object { Copy-Item $_.FullName $libApi }
# licences
$lic = NewDir (Join-Path $Out "LICENSES")
Copy-Item (Join-Path $LlvmSrc "llvm\LICENSE.TXT") (Join-Path $lic "LLVM-LICENSE.TXT")
Copy-Item (Join-Path $LlvmSrc "clang\LICENSE.TXT") (Join-Path $lic "clang-LICENSE.TXT")
Copy-Item (Join-Path $LlvmSrc "lld\LICENSE.TXT") (Join-Path $lic "lld-LICENSE.TXT")
if (Test-Path (Join-Path $Ndk "NOTICE")) { Copy-Item (Join-Path $Ndk "NOTICE") (Join-Path $lic "NDK-NOTICE.txt") }

# toolchain.txt
$llvmVer = (Select-String -Path (Join-Path $LlvmSrc "cmake\Modules\LLVMVersion.cmake") -Pattern 'set\(LLVM_VERSION_(MAJOR|MINOR|PATCH) (\d+)' |
    ForEach-Object { $_.Matches[0].Groups[2].Value }) -join "."
$ndkVer = (Get-Content (Join-Path $Ndk "source.properties") | Where-Object { $_ -like "Pkg.Revision*" }) -replace '.*=\s*', ''
$mb = (Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
@("xboxrecomp on-device C toolchain for Android arm64 (tools\android\make_device_toolchain.ps1)",
  "clang/lld: LLVM $llvmVer (host aarch64-linux-android, AArch64 only, static); resource dir lib/clang/$resVer",
  "runtime + sysroot: NDK $ndkVer, android-$Api; made $(Get-Date -Format s); $([math]::Round($mb,1)) MB",
  "",
  "Ship bin/clang and bin/ld.lld as lib*.so in nativeLibraryNewDir (extractNativeLibs=true); the rest as files.",
  "compile:  <clang> --driver-mode=gcc --target=aarch64-linux-android$Api --sysroot=<T>/sysroot -resource-dir=<T>/lib/clang/$resVer",
  "          `$(compile_flags.txt of the runtime SDK) -O2 -c <file.c> -o <file.o>",
  "link:     lld picks its flavor from argv[0] - shipped as lib*.so it needs '-flavor gnu' as the FIRST argument, so",
  "          call it directly (not through clang): <ld.lld> -flavor gnu <the link_template.txt line> (from -Test),",
  "          or through clang with --ld-path=<a file really named ld.lld>.") | Set-Content -Encoding ascii (Join-Path $Out "toolchain.txt")
Write-Host ("toolchain: {0} ({1:N1} MB), LLVM {2}, resource dir {3}" -f $Out, $mb, $llvmVer, $resVer)

if (-not $Test) { exit 0 }
# -Test: the minimal game against the runtime SDK, ONLY this folder's sysroot + resource dir (NDK clang 21 on the PC)
$cl = Join-Path $NdkTc "bin\clang.exe"
$T = (Resolve-Path $Out).Path -replace '\\', '/'
$S = (Resolve-Path $Test).Path -replace '\\', '/'
$wd = NewDir (Join-Path $Out "_test")
$src = Join-Path $wd "game.c"
@('#include "xbox_memory_layout.h"',
  '#include <stdio.h>',
  'recomp_func_t recomp_lookup(uint32_t va) { (void)va; return 0; }',
  'recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return 0; }',
  'int game_probe(void) { printf("probe\n"); return xbox_GetMemoryOffset() != 0; }') | Set-Content -Encoding ascii $src
function Expand($line) { return (($line -replace '\$SDK', $S -replace '\$SYSROOT', "$T/sysroot") -split '\s+' | Where-Object { $_ }) }
$cf = Expand (Get-Content (Join-Path $Test "compile_flags.txt"))
$ll = Expand (Get-Content (Join-Path $Test "link_line.txt"))
$common = @("--target=aarch64-linux-android$Api", "--sysroot=$T/sysroot", "-resource-dir=$T/lib/clang/$resVer")
& $cl @common @cf "-O2" "-c" $src "-o" (Join-Path $wd "game.o")
if ($LASTEXITCODE -ne 0) { throw "test compile failed" }
$linkArgs = @($common) + @("-shared", "-fuse-ld=lld", "-Wl,--no-undefined", "-Wl,-z,max-page-size=16384", "-o",
    (Join-Path $wd "libGAME.so"), (Join-Path $wd "game.o")) + @($ll)
& $cl @linkArgs
if ($LASTEXITCODE -ne 0) { throw "test link failed" }
# the linker call clang makes, as a template for the device's direct lld call
$hash = & $cl @linkArgs "-###" 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match 'ld\.lld' } | Select-Object -Last 1
# Placeholders: $LLD (the shipped ld.lld; "-flavor gnu" first, since a lib*.so name does not tell lld its flavor),
# $OUT (the game library), $OBJECTS (the game's object files), $TOOLCHAIN (this folder), $SDK (the runtime SDK).
$wdU = (Resolve-Path $wd).Path -replace '\\', '/'
$tpl = $hash.Trim() -replace '^"[^"]*ld\.lld(\.exe)?"', '"$LLD" "-flavor" "gnu"'
$tpl = $tpl -replace '\\\\', '/'
$tpl = $tpl -replace ('"' + [regex]::Escape("$wdU/libGAME.so") + '"'), '"$OUT"'
$tpl = $tpl -replace ('"' + [regex]::Escape("$wdU/game.o") + '"'), '$OBJECTS'
$tpl = $tpl -replace [regex]::Escape($T), '$TOOLCHAIN' -replace [regex]::Escape($S), '$SDK'
$tpl | Set-Content -Encoding ascii (Join-Path $Out "link_template.txt")
Write-Host ("test: libGAME.so {0:N2} MB linked with only this sysroot + resource dir; link_template.txt written" -f ((Get-Item (Join-Path $wd "libGAME.so")).Length / 1MB))
