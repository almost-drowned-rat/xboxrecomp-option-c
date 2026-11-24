# port_release.ps1 - a port's Windows release exe "<Name> Recomp.exe": the launcher with the payload appended (a
# local file in the port's private\deliver\ only, never committed). 2026-11-16: Panzer Dragoon Orta's
# tools/release.ps1 (Orta Recompiled; model THPS 2X tools/release.ps1) moved into the toolkit with the
# port's names in a config file (maintainer's order 2026-11-15, step 4: regress / release / E2E scripts).
#
#   powershell -ExecutionPolicy Bypass -File <toolkit>\tools\release\port_release.ps1 -Config <repo>\tools\release.psd1 -Version <name> [-Slim]
#
# The config (a .psd1 next to the port's own scripts; Orta's below as the example):
#   @{
#     Who        = "O (Panzer Dragoon Orta)"         # the lock record
#     Title      = "Panzer Dragoon Orta Recomp"      # README / licence texts
#     ExeName    = "Panzer Dragoon Orta Recomp.exe"  # the launcher target's output = the release exe
#     Target     = "orta_recomp"                     # the port's CMake target (build.ninja: CMakeFiles/<Target>.dir)
#     GameExe    = "ORTA.exe"                        # that target's output
#     GameName   = "ORTA"                            # the lifter's --game-name
#     GenMatch   = '[\\/]orta[\\/]gen[\\/][^\\/]+$'  # the generated-C include folder (becomes {GEN})
#     Rc         = "assets\orta.rc"                  # resource script + the files it needs, relative to the repo; it must
#                                                     #   hold a VERSIONINFO (ProductVersion = -Version) + a manifest
#                                                     #   (model: templates/game/assets/game.rc)
#     RcFiles    = @("assets\icon.ico")
#     Config     = @("config\game.toml", "config\variants\*.toml")   # copied to the payload at the same paths
#     Licence    = "LICENSE-Orta-recomp-GPL-3.0.txt"
#     PublicRepo = "Orta-Recompiled"
#     Data       = @("D:\...\pipelist_orta_pal.bin") # optional: files the launcher copies into data\ (launcher.c ship_data)
#     Extra      = @{ "shaders\librashader.dll" = "<path>" }   # optional: further payload files (path in payload = key)
#     Build      = "<private>\build\orta_recomp"     # optional: the build folder (default <Private>\build\<repo name>)
#     Repo       = "D:\src\my_port"   # optional: when the config does not sit in <repo>\tools\
#     Licences   = @{ "LICENSE-librashader.txt" = "<path>" }  # optional: further licence texts
#     LiftFile   = "tools\lift.txt"                  # optional: the port's own lift steps (lift.txt format: a name, then
#                                                     #   python's arguments, tab-separated; {XBE} {PIPE} {GEN} {TOOLS});
#                                                     #   default: Orta's steps (config\game.toml, --split 400)
#     LifterFiles = @{ "port\hooks.toml" = "config\hooks.toml"; "port\seeds" = "seeds\*_seeds.json" }   # optional:
#                                                     #   files the lift needs, copied under lifter\ ({TOOLS}/lifter/...)
#     PublicToolkit = "<clone of xboxrecomp-option-c>"   # a public release: licenses\source from this clone ...
#     PublicTag  = "v1.2.0"                               # ... at this tag (default: the version); without it the
#   }                                                     #     bundle is the private tree's, for test releases only
# Repo = the folder above the config's tools\ folder; Private = <repo>\..\private; Shared = <toolkit>\..\private.
# Needs a finished build of the port (its tools\build.ps1): it reads that build's build.ninja (compile flags, include
# folders, link line) and ninja's header list of the port's sources, so the player's machine builds exactly what
# this one does. Payload: the prebuilt libraries + the headers the port's sources include, the port's sources, the
# resource, config\, the lifter (tools/ packages + templates/runtime), the embeddable Python 3.12 + capstone,
# licences, data\ files, and the descriptors lift.txt / build.txt / toolchain.txt. Never in it: game data,
# generated C, the lifted game code. Staging in a NEW folder below private\release\.
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Console status output only.')]
param(
    [Parameter(Mandatory)] [string]$Config,
    [Parameter(Mandatory)] [string]$Version,
    # the shipped compiler zip cut to x86-64 (tools/release/prune_llvm_mingw.py; 181 -> 76 MB), made once into the
    # release cache and checked by its --smoke build (maintainer 2026-11-10: slim packages, not slower first start)
    [switch]$Slim,
    # staging + release exe here instead of private\release\ and private\deliver\ (comparisons, tests)
    [string]$Out,
    # the plain shared lock without the heavy slot (copying + zipping only; after asking the heavy slot's holder)
    [switch]$NoHeavy
)
$ErrorActionPreference = "Stop"
$C = Import-PowerShellDataFile -LiteralPath $Config
foreach ($k in "Who", "Title", "ExeName", "Target", "GameExe", "GameName", "GenMatch", "Rc", "Config", "Licence", "PublicRepo") {
    if (-not $C.ContainsKey($k)) { throw "$Config has no $k" }
}
$Toolkit = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Repo = if ($C.Repo) { $C.Repo } else { Split-Path -Parent (Split-Path -Parent ([IO.Path]::GetFullPath($Config))) }
$Private = Join-Path (Split-Path -Parent $Repo) "private"
$Shared = Join-Path (Split-Path -Parent $Toolkit) "private"
$Name = Split-Path -Leaf $Repo
$Build = if ($C.Build) { $C.Build } else { Join-Path $Private "build\$Name" }
$Cache = Join-Path $Shared "deps\release_cache"
$PyZip = Join-Path $Cache "python-3.12.10-embed-amd64.zip"
$PyZipSha = "4ACBED6DD1C744B0376E3B1CF57CE906F9DC9E95E68824584C8099A63025A3C3"
$Capstone = Join-Path $env:LOCALAPPDATA "Programs\Python\Python312\Lib\site-packages\capstone"   # winget's Python 3.12
$Deliver = if ($Out) { $Out } else { Join-Path $Private "deliver" }
$ExeName = $C.ExeName
$Target = $C.Target
$Toolchain = @{
    url    = "https://github.com/mstorsjo/llvm-mingw/releases/download/20260908/llvm-mingw-20260908-ucrt-x86_64.zip"
    sha256 = "1bcf74d06b724aeecaa6412ca85f5b26fb1da770e7cdcefa9263c9c5c3ad34b6"
    size   = "190677197"
    folder = "llvm-mingw-20260908-ucrt-x86_64"
}
foreach ($p in @($Repo, $Private, $Toolkit)) { if ($p -match ' ') { throw "path with a space: $p" } }
if ($Version.Length -gt 31 -or $Version -notmatch '^[A-Za-z0-9._-]+$') { throw "-Version: up to 31 characters A-Z a-z 0-9 . _ -" }
. (Join-Path $Toolkit "tools\lock.ps1")
function Norm([string]$p) { return ([IO.Path]::GetFullPath(($p -replace '/', '\'))).TrimEnd('\') }
function Fwd([string]$p) { return ($p -replace '\\', '/') }
$utf8 = New-Object Text.UTF8Encoding($false)

$lockArgs = @{ Class = "shared"; Who = $C.Who; What = "release: payload + launcher exe $Version" }
if (-not $NoHeavy) { $lockArgs.Heavy = $true }
$lock = Enter-GlobalLock @lockArgs
try {
    $ninja = Join-Path $Build "build.ninja"
    if (-not (Test-Path $ninja)) { throw "no build in $Build - run the port's tools\build.ps1 first" }
    $launcher = Join-Path $Build "launcher\$ExeName"
    if (-not (Test-Path -LiteralPath $launcher)) { throw "no launcher exe in $Build\launcher (toolkit without src/launcher?)" }
    if ((Get-FileHash $PyZip).Hash -ne $PyZipSha) { throw "$PyZip is not the pinned python-3.12.10-embed-amd64.zip" }
    $Stage = Join-Path $(if ($Out) { $Out } else { Join-Path $Private "release" }) ("{0}_{1}" -f (Get-Date -Format "yyyyMMdd_HHmmss"), $Version)
    if (Test-Path -LiteralPath $Stage) { throw "exists: $Stage" }
    $P = Join-Path $Stage "payload"
    New-Item -ItemType Directory $P | Out-Null
    $lines = Get-Content $ninja

    # -- the compile flags of the port's own sources (main.c's object) and its link line -----------------------
    $i = [array]::IndexOf($lines, ($lines | Where-Object { $_ -like "build CMakeFiles/$Target.dir/src/main.c.obj:*" } | Select-Object -First 1))
    if ($i -lt 0) { throw "main.c's object not found in build.ninja" }
    $vars = @{}
    for ($k = $i + 1; $k -lt $lines.Count -and $lines[$k] -match '^  (\w+) = (.*)$'; $k++) { $vars[$Matches[1]] = $Matches[2] }
    $exeRx = [regex]::Escape($C.GameExe)
    $ln = $lines | Where-Object { $_ -match "^build .*${exeRx}: C_EXECUTABLE_LINKER__$Target" } | Select-Object -First 1
    if (-not $ln) { throw "$($C.GameExe)'s link line not found in build.ninja" }
    $j = [array]::IndexOf($lines, $ln)
    $lvars = @{}
    for ($k = $j + 1; $k -lt $lines.Count -and $lines[$k] -match '^  (\w+) = (.*)$'; $k++) { $lvars[$Matches[1]] = $Matches[2] }
    $objs = (($ln -split ": C_EXECUTABLE_LINKER__${Target}_Release ", 2)[1] -split ' \| ')[0] -split ' '
    $portObjs = @($objs | Where-Object { $_ -like "CMakeFiles/$Target.dir/src/*.obj" })
    if (-not $portObjs) { throw "no port objects on the link line" }

    # include roots -> where they go in the payload ({TOOLS} = the player's tools\)
    $roots = [ordered]@{}
    $roots[(Norm (Join-Path $Repo "src"))] = "src"
    $roots[(Norm $Toolkit)] = "sdk/toolkit"
    $roots[(Norm (Join-Path $Shared "deps\install\include"))] = "sdk/include"
    $roots[(Norm $Build)] = "sdk/build"
    function Map-Path([string]$p) {
        $n = Norm $p
        foreach ($r in $roots.Keys) {
            if ($n -eq $r) { return $roots[$r] }
            if ($n.StartsWith("$r\", [StringComparison]::OrdinalIgnoreCase)) { return "$($roots[$r])/$(Fwd $n.Substring($r.Length + 1))" }
        }
        return $null
    }
    $cc = New-Object System.Collections.Generic.List[string]
    foreach ($f in ($vars["FLAGS"] -split ' ')) { if ($f -and $f -ne "-g" -and $f -ne "-gcodeview") { $cc.Add($f) } }
    foreach ($d in ($vars["DEFINES"] -split ' ')) {
        if (-not $d) { continue }
        # a development aid, never in a release (maintainer 2026-11-16; toolkit XBOXRECOMP_ICALL_FEEDBACK) - the
        # switch is the port's (its CMakeLists), so only said here, the build stays what the port built
        if ($d -eq "-DRECOMP_ICALL_FEEDBACK") { Write-Host "WARNING: RECOMP_ICALL_FEEDBACK is in the player's build (a development aid; remove it in the port's CMakeLists)" -ForegroundColor Yellow }
        $cc.Add($d)
    }
    $inc = $vars["INCLUDES"] -split ' '
    for ($k = 0; $k -lt $inc.Count; $k++) {
        $a = $inc[$k]
        if (-not $a) { continue }
        if ($a -eq "-isystem") { $k++; $a = "-I" + $inc[$k] }
        if ($a -notlike "-I*") { continue }
        $path = $a.Substring(2)
        if ($path -match $C.GenMatch) { $cc.Add("-I{GEN}"); continue }
        if ($path -like "*VulkanSDK*") { continue }                         # the port's sources include no Vulkan header
        $m = Map-Path $path
        if (-not $m) { throw "include folder $path is not under a known root" }
        $cc.Add("-I{TOOLS}/$m")
    }

    # -- the headers the port's sources include (ninja's list), our sources, config\, the generated port_icon.h -
    $ErrorActionPreference = "Continue"
    $deps = & ninja -C $Build -t deps @portObjs 2>$null
    $ErrorActionPreference = "Stop"
    $hdrs = New-Object System.Collections.Generic.HashSet[string]
    foreach ($d in $deps) {
        $t = $d.Trim()
        if (-not $t -or $t -like "CMakeFiles/*" -or $t -match '\.c$') { continue }
        if ($t -match 'llvm-mingw' -or $t -match '[\\/]gen[\\/]') { continue }   # the compiler's own / regenerated
        [void]$hdrs.Add((Norm $t))
    }
    $nh = 0
    foreach ($h in $hdrs) {
        $m = Map-Path $h
        if (-not $m) { throw "header $h is not under a known root" }
        $dst = Join-Path $P ($m -replace '/', '\')
        New-Item -ItemType Directory (Split-Path $dst) -Force | Out-Null
        Copy-Item -LiteralPath $h $dst
        $nh++
    }
    $srcLines = @()
    foreach ($o in $portObjs) {
        $rel = ($o -replace "^CMakeFiles/$([regex]::Escape($Target))\.dir/", '') -replace '\.obj$', ''
        $dst = Join-Path $P ($rel -replace '/', '\')
        New-Item -ItemType Directory (Split-Path $dst) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $Repo $rel) $dst
        $srcLines += "src`t{TOOLS}/$rel"
    }
    $rcLeaf = Split-Path -Leaf $C.Rc
    # The resource script names the exe (maintainer 2026-11-24, after two antivirus heuristics flagged Orta's zip: an
    # unsigned exe without any identity scores as suspicious): a VERSIONINFO with the port's names and this release's
    # version, and an application manifest (asInvoker) - the model is templates/game/assets/game.rc. The launcher and
    # the game the player's machine builds both carry it.
    $rcText = [IO.File]::ReadAllText((Join-Path $Repo $C.Rc))
    if ($rcText -notmatch '(?m)^\s*1\s+VERSIONINFO\b') { throw "$($C.Rc) has no '1 VERSIONINFO' block (templates/game/assets/game.rc)" }
    if ($rcText -notmatch '(?m)^\s*1\s+24\b') { throw "$($C.Rc) has no application manifest '1 24' (templates/game/assets/game.rc)" }
    $rcVer = [regex]::Match($rcText, 'VALUE\s+"ProductVersion"\s*,\s*"([^"]+)"').Groups[1].Value
    if (-not $rcVer -or -not $Version.StartsWith($rcVer)) {
        throw "$($C.Rc): ProductVersion '$rcVer' does not match -Version $Version (set FILEVERSION / PRODUCTVERSION / the version strings first)"
    }
    New-Item -ItemType Directory "$P\src\assets" -Force | Out-Null
    Copy-Item (Join-Path $Repo $C.Rc) "$P\src\assets\$rcLeaf"
    foreach ($f in @($C.RcFiles)) { if ($f) { Copy-Item (Join-Path $Repo $f) "$P\src\assets" } }
    foreach ($pat in @($C.Config)) {
        foreach ($f in @(Get-ChildItem (Join-Path $Repo $pat) -File)) {
            $rel = $f.FullName.Substring((Norm $Repo).Length + 1)
            $dst = Join-Path $P $rel
            New-Item -ItemType Directory (Split-Path $dst) -Force | Out-Null
            Copy-Item -LiteralPath $f.FullName $dst
        }
    }
    New-Item -ItemType Directory "$P\sdk\build" -Force | Out-Null
    Copy-Item "$Build\port_icon.h" "$P\sdk\build\port_icon.h"
    Write-Host "headers: $nh; port sources: $($portObjs.Count)"

    # -- shipped data (launcher.c ship_data: tools/data/* -> data/) and extra files ----------------------------
    foreach ($f in @($C.Data)) {
        if (-not $f) { continue }
        if (-not [IO.Path]::IsPathRooted($f)) { $f = Join-Path $Repo $f }   # e.g. "assets\pipelist_orta_pal.bin"
        if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { throw "data file missing: $f" }
        New-Item -ItemType Directory "$P\data" -Force | Out-Null
        Copy-Item -LiteralPath $f "$P\data\"
        Write-Host "data: $(Split-Path -Leaf $f) ($([math]::Round((Get-Item -LiteralPath $f).Length / 1KB, 1)) KB)"
    }
    if ($C.Extra) {
        foreach ($k in $C.Extra.Keys) {
            $dst = Join-Path $P $k
            New-Item -ItemType Directory (Split-Path $dst) -Force | Out-Null
            Copy-Item -LiteralPath $C.Extra[$k] $dst
        }
    }
    # -- the APU module (licence option C: the emulation as a replaceable shared library; launcher.c ship_data:
    #    tools/runtime/* -> next to the game exe). A build with XBOXRECOMP_APU_SHARED=OFF has none.
    $apuMod = Join-Path $Build "xboxrecomp\src\apu\xbox_apu.dll"
    if (-not (Test-Path -LiteralPath $apuMod)) {
        $apuMod = Get-ChildItem $Build -Recurse -Filter xbox_apu.dll -File | Select-Object -First 1 -ExpandProperty FullName
    }
    if ($apuMod) {
        New-Item -ItemType Directory "$P\runtime" -Force | Out-Null
        Copy-Item -LiteralPath $apuMod "$P\runtime\xbox_apu.dll"
        Write-Host "APU module: xbox_apu.dll ($([math]::Round((Get-Item -LiteralPath $apuMod).Length / 1KB)) KB)"
    } elseif (Select-String -Path (Join-Path $Build "CMakeCache.txt") -Pattern '^XBOXRECOMP_APU_SHARED:BOOL=ON' -Quiet) {
        throw "the APU module xbox_apu.dll is not in the build $Build"
    }

    # -- libraries (the link line, in order) --------------------------------------------------------------------
    New-Item -ItemType Directory "$P\sdk\lib" | Out-Null
    $link = New-Object System.Collections.Generic.List[string]
    foreach ($f in ($lvars["LINK_FLAGS"] -split ' ')) { if ($f -and $f -notlike "-Wl,--pdb=*") { $link.Add($f) } }
    # the player's game exe is a GUI program: no console window, also when started directly (the log is in data\)
    $link.Add("-Wl,--subsystem,windows"); $link.Add("-Wl,--entry=mainCRTStartup")
    $copied = @{}
    foreach ($a in ($lvars["LINK_LIBRARIES"] -split '\s+')) {
        if (-not $a) { continue }
        if ($a -like "-*") { $link.Add($a); continue }
        $src = if ([IO.Path]::IsPathRooted($a)) { $a } else { Join-Path $Build $a }
        $leaf = Split-Path $src -Leaf
        if (-not $copied.ContainsKey($leaf)) {
            Copy-Item -LiteralPath $src "$P\sdk\lib\$leaf"
            $copied[$leaf] = 1
        }
        $link.Add("{TOOLS}/sdk/lib/$leaf")
    }
    Write-Host "libraries: $($copied.Count) ($([math]::Round((Get-ChildItem "$P\sdk\lib" | Measure-Object Length -Sum).Sum / 1MB, 1)) MB)"

    # -- the lifter (the port's lift steps: config\game.toml + the variant file) ------------------------------
    $Lf = "$P\lifter"
    New-Item -ItemType Directory "$Lf\tools", "$Lf\templates" | Out-Null
    Copy-Item "$Toolkit\tools\__init__.py", "$Toolkit\tools\gameconfig.py", "$Toolkit\tools\variant_prefix.py" "$Lf\tools"
    foreach ($m in "xbe_parser", "disasm", "func_id", "abi_analysis", "recomp", "ghidra_naming") {
        $ErrorActionPreference = "Continue"
        robocopy "$Toolkit\tools\$m" "$Lf\tools\$m" /E /XD __pycache__ tests /XF "test_*.py" /NFL /NDL /NJH /NJS /NP | Out-Null
        $rc = $LASTEXITCODE
        $ErrorActionPreference = "Stop"
        if ($rc -ge 8) { throw "copy of tools\$m failed" }
    }
    Copy-Item "$Toolkit\templates\runtime" "$Lf\templates\runtime" -Recurse
    # a port with its own lift (THPS 2X: seeds, hooks, --split 1000, 2026-11-24): LifterFiles copies its files under
    # lifter\ (key = path under lifter\, value = source relative to the repo; a wildcard or a folder -> into that
    # folder), LiftFile is its lift.txt (the same format as below, used as it is - Windows and Linux share it)
    if ($C.LifterFiles) {
        foreach ($k in $C.LifterFiles.Keys) {
            $src = Join-Path $Repo $C.LifterFiles[$k]
            $dst = Join-Path $Lf $k
            $items = @(Get-ChildItem -Path $src -ErrorAction SilentlyContinue)
            if ($items.Count -eq 0) { throw "LifterFiles: nothing at $src" }
            if ($items.Count -gt 1 -or $items[0].PSIsContainer -or $C.LifterFiles[$k] -match '[*?]') {
                New-Item -ItemType Directory -Force $dst | Out-Null
                foreach ($it in $items) { Copy-Item $it.FullName $dst -Recurse }
            } else {
                New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
                Copy-Item $items[0].FullName $dst
            }
        }
    }
    $cfg = "{TOOLS}/config/game.toml"
    $lift = if ($C.LiftFile) { @(Get-Content -LiteralPath (Join-Path $Repo $C.LiftFile) -Encoding UTF8) } else { @(
        "# the lift (the same steps as the port's tools\lift.ps1): name, then python's arguments; {XBE} {PIPE} {GEN} {TOOLS}",
        "mkdir`t{PIPE}/disasm`t{PIPE}/func_id`t{PIPE}/abi`t{PIPE}/recomp`t{GEN}",
        "xbe_parser`t-m`ttools.xbe_parser`t{XBE}`t--json`t{PIPE}/xbe.json`t--quiet",
        "disasm`t-m`ttools.disasm`t{XBE}`t-o`t{PIPE}/disasm`t--analysis-json`t{PIPE}/xbe.json`t--force`t--no-asm`t--game-config`t$cfg",
        "func_id`t-m`ttools.func_id`t{XBE}`t--functions`t{PIPE}/disasm/functions.json`t--strings`t{PIPE}/disasm/strings.json`t--xrefs`t{PIPE}/disasm/xrefs.json`t-o`t{PIPE}/func_id",
        "abi_analysis`t-m`ttools.abi_analysis`t{XBE}`t--disasm-dir`t{PIPE}/disasm`t--func-id-dir`t{PIPE}/func_id`t--output-dir`t{PIPE}/abi",
        # --split 400 + a fresh weight-balanced partition layout: ~47 files of even compile cost instead of
        # chunks of 1000 functions (Orta: one 12.6 MB file of 19 held the parallel compile back; 2026-11-16, 6a)
        "recomp`t-m`ttools.recomp`t{XBE}`t--all`t--game-config`t$cfg`t--game-name`t$($C.GameName)`t--gen-dir`t{GEN}`t--disasm-dir`t{PIPE}/disasm`t--func-id-dir`t{PIPE}/func_id`t--abi-dir`t{PIPE}/abi`t-o`t{PIPE}/recomp`t--split`t400`t--partitions`t{PIPE}/partitions.json`t--repartition"
    ) }
    if (-not ($lift | Where-Object { $_ -match "^recomp`t" })) { throw "the lift has no recomp line ($(if ($C.LiftFile) { $C.LiftFile } else { 'default' }))" }
    [IO.File]::WriteAllText("$P\lift.txt", (($lift -join "`r`n") + "`r`n"), $utf8)

    # -- Python (embeddable) + capstone ----------------------------------------------------------------------------
    Expand-Archive -LiteralPath $PyZip -DestinationPath "$P\python"
    # Only what the lifter uses (maintainer 2026-11-24, antivirus heuristics): it needs ctypes (capstone), subprocess,
    # hashlib (the built-in SHA-2 when _hashlib is gone) and plain-Python modules. Network, TLS / crypto, databases,
    # installer, sound, async and multiprocessing modules go - a launcher that downloads nothing carries no TLS - and
    # pythonw.exe (toolchain.txt names python.exe).
    foreach ($f in "_ssl.pyd", "libssl-3.dll", "libcrypto-3.dll", "_hashlib.pyd", "_socket.pyd", "_sqlite3.pyd",
                   "sqlite3.dll", "_msi.pyd", "_wmi.pyd", "winsound.pyd", "_asyncio.pyd", "_overlapped.pyd",
                   "_multiprocessing.pyd", "pythonw.exe") {
        $fp = Join-Path "$P\python" $f
        if (-not (Test-Path -LiteralPath $fp)) { throw "the embeddable Python has no $f (pinned zip changed?)" }
        Remove-Item -LiteralPath $fp
    }
    New-Item -ItemType Directory "$P\python\Lib\site-packages" | Out-Null
    Copy-Item $Capstone "$P\python\Lib\site-packages\capstone" -Recurse
    Get-ChildItem "$P\python\Lib\site-packages\capstone" -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force
    $pth = Get-ChildItem "$P\python" -Filter "python*._pth" | Select-Object -First 1
    [IO.File]::WriteAllLines($pth.FullName, @("python312.zip", ".", "Lib\site-packages", "..\lifter"))

    # -- licences, descriptors ----------------------------------------------------------------------------------
    New-Item -ItemType Directory "$P\licenses" | Out-Null
    # every licence text of what the release carries or the launcher links; a missing one stops the release
    $Mingw = Join-Path (Split-Path (Split-Path (Get-Command clang.exe).Source)) "x86_64-w64-mingw32\share\mingw32"
    $lic = [ordered]@{
        $C.Licence                                 = "$Repo\LICENSE"
        "LICENSE-xboxrecomp-toolkit-MIT.txt"       = "$Toolkit\LICENSE"
        "NOTICE-toolkit.txt"                       = "$Toolkit\NOTICE"
        "LICENSE-xemu-parts-LGPL-2.1.txt"          = "$Toolkit\LICENSES\LGPL-2.1.txt"
        "LICENSE-pl_mpeg-MIT.txt"                  = "$Toolkit\src\bootvideo\third_party\pl_mpeg\LICENSE"
        "LICENSE-miniz-MIT.txt"                    = "$Toolkit\src\shaderpack\third_party\miniz\LICENSE"
        "LICENSE-SMAA-MIT.txt"                     = "$Toolkit\src\nv2a_vk\third_party\smaa\LICENSE.txt"
        "LICENSE-FSR1-MIT.txt"                     = "$Toolkit\src\nv2a_vk\third_party\fsr1\LICENSE.txt"
        "LICENSE-DejaVu-fonts.txt"                 = "$Toolkit\src\nv2a_vk\third_party\dejavu\LICENSE"
        "LICENSE-SDL3-zlib.txt"                    = (Join-Path $Shared "deps\src\SDL3\LICENSE.txt")
        "LICENSE-glslang.txt"                      = (Join-Path $Shared "deps\src\glslang\LICENSE.txt")
        "LICENSE-Vulkan-SDK.txt"                   = (Get-ChildItem "C:\VulkanSDK\*\Licenses\LICENSE.txt" | Select-Object -Last 1).FullName
        "LICENSE-capstone-BSD.txt"                 = (Join-Path (Split-Path $Capstone) "capstone-5.0.9.dist-info\LICENSE.TXT")
        "LICENSE-Python.txt"                       = "$P\python\LICENSE.txt"
        "LICENSE-llvm-mingw-Apache-2.0-LLVM.txt"   = (Join-Path (Split-Path (Split-Path (Get-Command clang.exe).Source)) "LICENSE.TXT")
        "COPYING-MinGW-w64-runtime.txt"            = "$Mingw\COPYING.MinGW-w64-runtime.txt"
        "COPYING-winpthreads.txt"                  = "$Mingw\COPYING.winpthreads.txt"
        "THIRD_PARTY_NOTICES.txt"                  = "$Toolkit\THIRD_PARTY_NOTICES.txt"
    }
    if ($C.Licences) { foreach ($k in $C.Licences.Keys) { $lic[$k] = $C.Licences[$k] } }   # e.g. librashader
    foreach ($k in $lic.Keys) {
        if (-not $lic[$k] -or -not (Test-Path -LiteralPath $lic[$k])) { throw "licence text missing for ${k}: $($lic[$k])" }
        Copy-Item -LiteralPath $lic[$k] "$P\licenses\$k"
    }
    # licence option C (maintainer 2026-11-17): the corresponding source of the LGPL parts (the xemu APU core with
    # this project's changes, the headers it needs) in every release - licenses\source\, with SOURCE.txt
    # The files come from the PUBLIC toolkit snapshot at the release's tag (config PublicToolkit = its clone,
    # PublicTag default = the version): the private tree's comments are not scrubbed and its commits are not public.
    # Without PublicToolkit (a test release) the private tree is used and SOURCE.txt says so - never publish that.
    $lgplArgs = @((Join-Path $Toolkit "tools\release\lgpl_source.py"))
    if ($C.PublicToolkit) {
        $ptag = if ($C.PublicTag) { $C.PublicTag } else { $Version }
        $psha = (git -C $C.PublicToolkit rev-parse "$ptag^{commit}").Trim()
        if ($LASTEXITCODE -ne 0 -or -not $psha) { throw "public toolkit $($C.PublicToolkit) has no tag $ptag" }
        $ptree = Join-Path $Stage "public_toolkit"
        if (Test-Path $ptree) { Remove-Item $ptree -Recurse -Force }
        New-Item -ItemType Directory $ptree | Out-Null
        git -C $C.PublicToolkit archive --format=tar $psha -o "$ptree.tar"
        tar -xf "$ptree.tar" -C $ptree
        Remove-Item "$ptree.tar"
        $lgplArgs += @($ptree, "$P\licenses\source", "--version", $Version,
                       "--commit", "xboxrecomp-option-c, tag $ptag, commit $psha",
                       "--notices", "$Toolkit\THIRD_PARTY_NOTICES.txt")
    } else {
        Write-Warning "no PublicToolkit in the config: licenses\source comes from the PRIVATE tree - a test release only"
        $lgplArgs += @($Toolkit, "$P\licenses\source", "--version", "$Version (PRIVATE TREE - not for publishing)")
    }
    & "$P\python\python.exe" -I @lgplArgs
    if ($LASTEXITCODE -ne 0) { throw "lgpl_source.py failed - the LGPL source bundle is incomplete" }
    $T = $C.Title
    [IO.File]::WriteAllText("$P\licenses\README.txt", ("$T $Version - licences of everything in this release.`r`n`r`n" +
        "$T (the port's own code): GPL-3.0-or-later. Built on the xboxrecomp toolkit, branch option-c`r`n" +
        "(MIT; its third-party parts in NOTICE-toolkit.txt: the xemu parts LGPL-2.1-or-later, pl_mpeg / miniz / SMAA /`r`n" +
        "dr_libs / stb_vorbis / stb_truetype / burnout3-sw MIT or public domain, the DejaVu Sans Mono Bold font of the`r`n" +
        "status overlays under the Bitstream Vera / Arev licence). Linked in: SDL3 (zlib), glslang (BSD / MIT / Apache-2.0),`r`n" +
        "the Vulkan loader's import library (Apache-2.0), the llvm-mingw runtime (libc++ etc.: Apache-2.0 with LLVM exception;`r`n" +
        "MinGW-w64 runtime, winpthreads). Shipped for the translator: Python 3.12.10 embeddable (PSF licence) with capstone`r`n" +
        "5.0.9 (BSD). Fetched or carried for the first start: llvm-mingw 20260908 (https://github.com/mstorsjo/llvm-mingw).`r`n`r`n" +
        "THIRD_PARTY_NOTICES.txt: every third-party part with its copyright and licence.`r`n" +
        "source\: the complete source of the LGPL-2.1-or-later parts (the xemu MCPX APU core with this project's changes)`r`n" +
        "as built into this release; source\SOURCE.txt says how to rebuild that library and relink the game with it.`r`n`r`n" +
        "Source of this release: the public $($C.PublicRepo) repository, tag $Version.`r`n"), $utf8)
    $build = @("# compile + link (from $Name's build.ninja): cc = the common arguments, src / gen = the units, rc, link") +
             @("cc`t" + ($cc -join "`t")) + $srcLines + @("gen`t*.c", "rc`t{TOOLS}/src/assets/$rcLeaf`t/I`t{TOOLS}/src/assets",
             ("link`t" + ($link -join "`t")))
    [IO.File]::WriteAllText("$P\build.txt", (($build -join "`r`n") + "`r`n"), $utf8)
    # toolchain.txt format 2: no URL - the compiler zip ships in the release zip's tools\bundle\ and the launcher
    # only checks + unpacks it into tools\llvm-mingw; paths relative to tools\
    $zipName = Split-Path -Leaf $Toolchain.url
    if ($Slim) {
        $full = Join-Path $Cache $zipName
        $zipName = $zipName -replace '\.zip$', '-slim.zip'
        $slimZip = Join-Path $Cache $zipName
        if (-not (Test-Path -LiteralPath $slimZip)) {
            if ((Get-FileHash -LiteralPath $full).Hash -ne $Toolchain.sha256.ToUpperInvariant()) { throw "$full is not the pinned llvm-mingw zip" }
            $py = Join-Path $env:LOCALAPPDATA "Programs\Python\Python312\python.exe"
            & $py -I (Join-Path $Toolkit "tools\release\prune_llvm_mingw.py") $full $slimZip --smoke
            if ($LASTEXITCODE -ne 0) { if (Test-Path -LiteralPath $slimZip) { Remove-Item -LiteralPath $slimZip }; throw "prune_llvm_mingw.py failed" }
        }
        $Toolchain.sha256 = (Get-FileHash -LiteralPath $slimZip).Hash.ToLowerInvariant()
        $Toolchain.size = [string](Get-Item -LiteralPath $slimZip).Length
        Write-Host "slim compiler zip: $zipName ($([math]::Round([int64]$Toolchain.size / 1MB, 1)) MB)"
    }
    $tcl = @("format`t2",
             "clang`tllvm-mingw/bin/clang.exe",
             "python`tpython/python.exe",
             "rc`tllvm-mingw/bin/llvm-rc.exe",
             "bundle`tbundle/$zipName`t$($Toolchain.sha256)`t$($Toolchain.size)`t$($Toolchain.folder)`tllvm-mingw")
    [IO.File]::WriteAllText("$P\toolchain.txt", (($tcl -join "`r`n") + "`r`n"), $utf8)
    [IO.File]::WriteAllText("$P\README.txt", ("$T $Version - files the launcher unpacked (tools\).`r`nAI disclosure:  AI assistance yes (Claude Code by Anthropic).`r`n" +
        "Nothing here is game code or game data: the game is translated from your own disc image on your machine.`r`n" +
        "Licences: licenses\ (the toolkit's NOTICE lists every third-party part).`r`n"), $utf8)

    # -- zip the payload (deflate; the launcher's reader: no ZIP64, entries up to 64 MB) ----------------------
    $files = @(Get-ChildItem $P -Recurse -File)
    $big = $files | Where-Object { $_.Length -ge 64MB }
    if ($big) { throw "payload entries over 64 MB: $($big.FullName -join ', ')" }
    if ($files.Count -ge 65535) { throw "payload has $($files.Count) files (no ZIP64)" }
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zip = Join-Path $Stage "payload.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($P, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
    Write-Host "payload: $($files.Count) files, $([math]::Round(($files | Measure-Object Length -Sum).Sum / 1MB, 1)) MB -> $([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB zipped"

    # -- launcher exe + payload + trailer ------------------------------------------------------------------------
    if (-not (Test-Path $Deliver)) { New-Item -ItemType Directory $Deliver | Out-Null }
    $out = Join-Path $Deliver $ExeName
    $exeBytes = [IO.File]::ReadAllBytes($launcher)
    $zipBytes = [IO.File]::ReadAllBytes($zip)
    $trailer = New-Object byte[] 48
    [Text.Encoding]::ASCII.GetBytes("XRPAYLD1").CopyTo($trailer, 0)
    [BitConverter]::GetBytes([uint64]$exeBytes.Length).CopyTo($trailer, 8)
    [Text.Encoding]::ASCII.GetBytes($Version).CopyTo($trailer, 16)
    $fs = [IO.File]::Create($out)
    $fs.Write($exeBytes, 0, $exeBytes.Length); $fs.Write($zipBytes, 0, $zipBytes.Length); $fs.Write($trailer, 0, 48)
    $fs.Close()
    Write-Host "release exe: $out ($([math]::Round((Get-Item -LiteralPath $out).Length / 1MB, 1)) MB, version $Version)"
    [IO.File]::WriteAllText("$Stage\release.txt", "version $Version`r`nexe $out`r`nsha256 $((Get-FileHash -LiteralPath $out).Hash)`r`n", $utf8)
} finally {
    Exit-GlobalLock $lock
}
