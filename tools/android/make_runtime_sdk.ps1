# make_runtime_sdk.ps1 - the toolkit's Android runtime SDK: everything the game library needs to be compiled and
# linked ON THE DEVICE (toolkit static libraries + headers + libSDL3.so + libxr_glslang.so + the exact compile and
# link lines), built on a Windows PC with the NDK. Reusable for every port (Orta first, 2026-11-11).
#
#   powershell -ExecutionPolicy Bypass -File tools\android\make_runtime_sdk.ps1 -Ndk <android-ndk-rXX>
#       -SdlSrc <SDL3 source> -GlslangSrc <glslang source> -Work <build folder> -Out <SDK folder> [-Api 29] [-Jobs 4]
#
# Out\ (replaced on every run; refused if it exists without the sdk.txt marker):
#   lib\              toolkit lib*.a (as linked), libSDL3.so, libxr_glslang.so
#   include\toolkit\  the toolkit's src\ headers (tree kept), include\ext<N>\ the other include dirs (SDL3, glslang,
#                     generated build info)
#   compile_flags.txt one line: target, sysroot, defines, include dirs - paths as $SDK/..., the sysroot as $SYSROOT
#   link_line.txt     one line: the libraries in link order ($SDK/lib/...) and the system libraries (-l...; the
#                     NDK's libvulkan.so stub is not copied - the device has the real one)
#   game_must_define.txt  symbols the toolkit libraries use and nothing in the SDK or the system defines - the
#                     generated code / recomp_manual.c has to (recomp_lookup, ...)
#   sdk.txt           toolkit commit, NDK, API level, date
# Built with: SDL3 shared, glslang static (ENABLE_OPT=OFF) -> libxr_glslang.so (tools\android\xr_glslang), the
# toolkit with the Vulkan renderer (D3D11 off) through tools\android\sdk_probe. -fPIC everywhere (NDK default, and
# CMAKE_POSITION_INDEPENDENT_CODE=ON). ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON (16 KB pages).
param(
    [Parameter(Mandatory = $true)][string]$Ndk,
    [Parameter(Mandatory = $true)][string]$SdlSrc,
    [Parameter(Mandatory = $true)][string]$GlslangSrc,
    [Parameter(Mandatory = $true)][string]$Work,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$Api = 29,
    [int]$Jobs = 4
)
# Continue, not Stop: Windows PowerShell 5.1 turns a native tool's stderr line under *> into a terminating
# NativeCommandError (cmake warnings); every native step checks $LASTEXITCODE.
$ErrorActionPreference = "Continue"
$Toolkit = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Tc = Join-Path $Ndk "build\cmake\android.toolchain.cmake"
$Bin = Join-Path $Ndk "toolchains\llvm\prebuilt\windows-x86_64\bin"
$Sysroot = Join-Path $Ndk "toolchains\llvm\prebuilt\windows-x86_64\sysroot"
$Nm = Join-Path $Bin "llvm-nm.exe"
foreach ($p in @($Tc, $Nm, $SdlSrc, $GlslangSrc)) { if (-not (Test-Path $p)) { throw "missing: $p" } }
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$Work = (Resolve-Path $Work).Path
function Cm($p) { return ($p -replace '\\', '/') }
$Common = @("-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_TOOLCHAIN_FILE=$(Cm $Tc)",
            "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-$Api", "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
function Step($name, $cfgArgs, $buildArgs) {
    $log = Join-Path $Work "$name.log"
    Write-Host "== $name"
    & cmake @cfgArgs *> $log
    if ($LASTEXITCODE -ne 0) { Get-Content $log -Tail 20; throw "${name}: configure failed (log $log)" }
    & cmake @buildArgs *>> $log
    if ($LASTEXITCODE -ne 0) { Get-Content $log -Tail 30; throw "${name}: build failed (log $log)" }
}

# 1. SDL3 (shared, as in the APK)
$SdlPrefix = Join-Path $Work "sdl3"
if (-not (Test-Path (Join-Path $SdlPrefix "lib\libSDL3.so"))) {
    $b = Join-Path $Work "sdl3_build"
    Step "sdl3" (@("-S", $SdlSrc, "-B", $b) + $Common + @("-DCMAKE_INSTALL_PREFIX=$(Cm $SdlPrefix)", "-DSDL_SHARED=ON",
        "-DSDL_STATIC=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF")) @("--build", $b, "-j", "$Jobs", "--target", "install")
}
# 2. glslang (static, no optimizer, no tools)
$GlPrefix = Join-Path $Work "glslang"
if (-not (Test-Path (Join-Path $GlPrefix "lib\cmake\glslang\glslang-config.cmake"))) {
    $b = Join-Path $Work "glslang_build"
    Step "glslang" (@("-S", $GlslangSrc, "-B", $b) + $Common + @("-DCMAKE_INSTALL_PREFIX=$(Cm $GlPrefix)",
        "-DBUILD_SHARED_LIBS=OFF", "-DENABLE_OPT=OFF", "-DENABLE_GLSLANG_BINARIES=OFF", "-DGLSLANG_TESTS=OFF",
        "-DENABLE_HLSL=OFF", "-DGLSLANG_ENABLE_INSTALL=ON", "-DENABLE_SPVREMAPPER=OFF")) @("--build", $b, "-j", "$Jobs", "--target", "install")
}
# 3. libxr_glslang.so
$XgB = Join-Path $Work "xr_glslang"
Step "xr_glslang" (@("-S", (Join-Path $Toolkit "tools\android\xr_glslang"), "-B", $XgB) + $Common + @("-DANDROID_STL=c++_static",
    "-Dglslang_DIR=$(Cm (Join-Path $GlPrefix 'lib\cmake\glslang'))")) @("--build", $XgB, "-j", "$Jobs", "--target", "xr_glslang")
$XgSo = Join-Path $XgB "libxr_glslang.so"
# 4. the toolkit through the probe
$PrB = Join-Path $Work "probe"
if (Test-Path (Join-Path $PrB "CMakeCache.txt")) { Remove-Item (Join-Path $PrB "CMakeCache.txt") }
Step "probe" (@("-S", (Join-Path $Toolkit "tools\android\sdk_probe"), "-B", $PrB) + $Common + @("-DXR_TOOLKIT=$(Cm $Toolkit)",
    "-DXR_SDL3_SO=$(Cm (Join-Path $SdlPrefix 'lib\libSDL3.so'))", "-DXR_SDL3_INCLUDE=$(Cm (Join-Path $SdlPrefix 'include'))",
    "-DXR_GLSLANG_SO=$(Cm $XgSo)", "-DXR_GLSLANG_INCLUDE=$(Cm (Join-Path $GlPrefix 'include'))")) @("--build", $PrB, "-j", "$Jobs")

# 5. the compile and link commands of the stub, from build.ninja
$ninja = Get-Content (Join-Path $PrB "build.ninja")
function Block($head) {
    $i = 0
    while ($i -lt $ninja.Count -and -not $ninja[$i].StartsWith($head)) { $i++ }
    if ($i -ge $ninja.Count) { throw "build.ninja: no block '$head'" }
    $vars = @{}
    for ($j = $i + 1; $j -lt $ninja.Count -and $ninja[$j].StartsWith("  "); $j++) {
        $k, $v = $ninja[$j].Trim() -split " = ", 2
        $vars[$k] = $v
    }
    return $vars
}
function Unescape($s) { return ($s -replace '\$:', ':' -replace '\$ ', ' ' -replace '\$\$', '$') }
function Tokens($s) { if (-not $s) { return @() }; return @((Unescape $s) -split '\s+' | Where-Object { $_ }) }
$cc = Block "build CMakeFiles/sdk_probe.dir/"
$ln = Block "build libsdk_probe.so"

# 6. assemble Out
if (Test-Path $Out) {
    if (-not (Test-Path (Join-Path $Out "sdk.txt"))) { throw "$Out exists and is not an SDK made by this script (no sdk.txt) - not touched" }
    Remove-Item -Recurse -Force $Out
}
New-Item -ItemType Directory -Force -Path (Join-Path $Out "lib"), (Join-Path $Out "include") | Out-Null
$ToolkitSrc = Join-Path $Toolkit "src"
$sysU = Cm $Sysroot

# headers: the toolkit tree once, every other include dir as ext<N>
$incMap = @{}; $ext = 0
Get-ChildItem $ToolkitSrc -Recurse -Include *.h, *.inc | ForEach-Object {
    $rel = $_.FullName.Substring($ToolkitSrc.Length + 1)
    $dst = Join-Path $Out "include\toolkit\$rel"
    New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
    Copy-Item $_.FullName $dst
}
$incFlags = @()
$toks = Tokens $cc["INCLUDES"]
for ($i = 0; $i -lt $toks.Count; $i++) {
    $t = $toks[$i]; $flag = "-I"
    if ($t -eq "-isystem") { $flag = "-isystem "; $i++; $t = $toks[$i] } elseif ($t.StartsWith("-I")) { $t = $t.Substring(2) } else { continue }
    $d = $t.Trim('"')
    if (-not [IO.Path]::IsPathRooted($d)) { $d = Join-Path $PrB $d }
    $d = [IO.Path]::GetFullPath($d)
    if ($d.StartsWith($ToolkitSrc, [StringComparison]::OrdinalIgnoreCase)) {
        $rel = $d.Substring($ToolkitSrc.Length).TrimStart('\')
        $sdkPath = if ($rel) { "`$SDK/include/toolkit/" + (Cm $rel) } else { "`$SDK/include/toolkit" }
    } else {
        if (-not $incMap.ContainsKey($d)) {
            $incMap[$d] = "ext$ext"; $ext++
            $dst = Join-Path $Out ("include\" + $incMap[$d])
            New-Item -ItemType Directory -Force -Path $dst | Out-Null
            if (Test-Path $d) {
                Get-ChildItem $d -Recurse -Include *.h, *.hpp, *.inc | ForEach-Object {
                    $r = $_.FullName.Substring($d.Length).TrimStart('\')
                    $f = Join-Path $dst $r
                    New-Item -ItemType Directory -Force -Path (Split-Path $f) | Out-Null
                    Copy-Item $_.FullName $f
                }
            }
        }
        $sdkPath = "`$SDK/include/" + $incMap[$d]
    }
    $incFlags += "$flag$sdkPath"
}
# No -O / -g / NDEBUG from the probe's build type: the game's own compile line chooses those.
$flags = @(Tokens $cc["FLAGS"] | ForEach-Object { $_ -replace [regex]::Escape($sysU), '$SYSROOT' } |
           Where-Object { $_ -notmatch '^-O' -and $_ -ne '-DNDEBUG' -and $_ -ne '-g' })
$defs = @(Tokens $cc["DEFINES"] | Where-Object { $_ -notmatch '_EXPORTS$' })
# The target is not in FLAGS (CMake passes it as CMAKE_C_COMPILER_TARGET): named here, the sysroot as $SYSROOT.
(@("--target=aarch64-linux-android$Api", "--sysroot=`$SYSROOT") + @($flags) + @($defs) + @($incFlags)) -join " " |
    Set-Content -Encoding ascii (Join-Path $Out "compile_flags.txt")

# libraries in link order
$linkOut = @()
foreach ($t in (Tokens $ln["LINK_LIBRARIES"])) {
    if ($t -match '\.(a|so)$') {
        $p = if ([IO.Path]::IsPathRooted($t)) { $t } else { Join-Path $PrB $t }
        $p = [IO.Path]::GetFullPath($p)
        $name = Split-Path $p -Leaf
        if ($p.StartsWith($Sysroot, [StringComparison]::OrdinalIgnoreCase)) {   # a system library (libvulkan.so): the device has it
            $linkOut += "-l" + ($name -replace '^lib', '' -replace '\.(a|so)$', '')
            continue
        }
        if (-not (Test-Path (Join-Path $Out "lib\$name"))) { Copy-Item $p (Join-Path $Out "lib\$name") }
        $linkOut += "`$SDK/lib/$name"
    } else {
        $linkOut += ($t -replace [regex]::Escape($sysU), '$SYSROOT')
    }
}
($linkOut -join " ") | Set-Content -Encoding ascii (Join-Path $Out "link_line.txt")

# 7. what the game must define: undefined in the toolkit .a, defined nowhere in the SDK or the system libraries
$def = New-Object 'System.Collections.Generic.HashSet[string]'
$und = New-Object 'System.Collections.Generic.HashSet[string]'
Get-ChildItem (Join-Path $Out "lib") -Filter *.a | ForEach-Object {
    & $Nm --defined-only --format=just-symbols $_.FullName 2>$null | ForEach-Object { [void]$def.Add($_) }
    & $Nm -u --format=just-symbols $_.FullName 2>$null | ForEach-Object { [void]$und.Add($_) }
}
# the system: the API level's shared stubs (versioned names, "fopen@@LIBC"), the static sysroot libraries
# (libatomic.a ...) and the compiler's builtins (libclang_rt.builtins: the __aarch64_* outline atomics)
$sysLibs = Get-ChildItem (Join-Path $Sysroot "usr\lib\aarch64-linux-android\$Api") -Filter *.so
@(Get-ChildItem (Join-Path $Out "lib") -Filter *.so) + @($sysLibs) | ForEach-Object {
    & $Nm -D --defined-only --format=just-symbols $_.FullName 2>$null | ForEach-Object { [void]$def.Add(($_ -replace '@.*$', '')) }
}
$sysStatic = @(Get-ChildItem (Join-Path $Sysroot "usr\lib\aarch64-linux-android") -Filter *.a) +
             @(Get-ChildItem (Join-Path $Sysroot "usr\lib\aarch64-linux-android\$Api") -Filter crt*.o) +
             @(Get-ChildItem (Join-Path $Ndk "toolchains\llvm\prebuilt\windows-x86_64\lib") -Recurse -Filter "libclang_rt.builtins-aarch64-android.a")
$sysStatic | ForEach-Object {
    & $Nm --defined-only --format=just-symbols $_.FullName 2>$null | ForEach-Object { [void]$def.Add($_) }
}
$need = @($und | Where-Object { -not $def.Contains($_) } | Sort-Object)
$need | Set-Content -Encoding ascii (Join-Path $Out "game_must_define.txt")

# 8. sdk.txt
$commit = (& git -C $Toolkit rev-parse --short HEAD) 2>$null
$dirty = if ((& git -C $Toolkit status --porcelain) 2>$null) { " (dirty)" } else { "" }
$ndkVer = (Get-Content (Join-Path $Ndk "source.properties") | Where-Object { $_ -like "Pkg.Revision*" }) -replace '.*=\s*', ''
@("xboxrecomp Android runtime SDK (tools\android\make_runtime_sdk.ps1)",
  "toolkit: $commit$dirty", "ndk: $ndkVer", "abi: arm64-v8a, android-$Api, flexible page sizes", "made: $(Get-Date -Format s)",
  "use: clang `$(cat compile_flags.txt) -c <game sources>; clang -shared -o libGAME.so <objects> `$(cat link_line.txt)",
  "     with SDK=<this folder> and SYSROOT=<the on-device sysroot>") | Set-Content -Encoding ascii (Join-Path $Out "sdk.txt")

$size = (Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("SDK: {0} ({1:N1} MB); {2} libraries; game must define {3} symbols" -f $Out, $size,
    (Get-ChildItem (Join-Path $Out "lib")).Count, $need.Count)
