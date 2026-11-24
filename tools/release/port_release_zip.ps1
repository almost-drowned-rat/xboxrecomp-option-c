# port_release_zip.ps1 - the player's Windows release zip from the exe tools\release\port_release.ps1 built. B
# 2026-11-16: Panzer Dragoon Orta's tools/release_zip.ps1 (Orta Recompiled; model THPS 2X) moved into
# the toolkit with the port's names in the same config as port_release.ps1 (maintainer's order 2026-11-15, step 4).
#
#   powershell -File <toolkit>\tools\release\port_release_zip.ps1 -Config <repo>\tools\release.psd1 -Version <v> -Notes <release_notes.md> [-Folder <name>] [-Out <dir>]
#
# Config keys used here (port_release.ps1's header has the rest): Who, ExeName, ZipPrefix ("Orta-Recompiled": the
# zip is <ZipPrefix>-v<v>-win64.zip), Readme (the player's README template relative to the repo, "{VERSION}"
# replaced; Orta: "tools\release_readme.txt"), RecompLauncher (the port's recomplauncher.json relative to the repo,
# required since 2026-11-24: copied to the top of the zip next to the exe, checked for JSON / "format": 1 / 1 MiB).
# Into a new folder <deliver>\v<v>\ (refused if it exists): zip\ (the exe, README.txt, LICENSES\ from the release's
# payload, tools\bundle\ with the pinned llvm-mingw zip), the zip, SHA256SUMS.txt, release_notes.md. The compiler ships
# inside the zip, so the first start needs no internet (toolchain.txt format 2: a missing or damaged zip ends with
# "The package is incomplete - please download it again."). The llvm-mingw zip is the unchanged upstream release
# (Apache-2.0 with LLVM exception, licence in LICENSES\), cached once in tools\private\deps\release_cache.
param(
    [Parameter(Mandatory)] [string]$Config,
    [Parameter(Mandatory)] [string]$Version,
    [Parameter(Mandatory)] [string]$Notes,
    [string]$Folder,                                 # the folder name below the deliver folder (default v<Version>)
    [string]$Out                                     # port_release.ps1's -Out (its exe + stage there)
)
$ErrorActionPreference = "Stop"
$C = Import-PowerShellDataFile -LiteralPath $Config
foreach ($k in "Who", "ExeName", "ZipPrefix", "Readme") { if (-not $C.ContainsKey($k)) { throw "$Config has no $k" } }
if (-not $C.ContainsKey("RecompLauncher")) {
    throw "$Config has no RecompLauncher (the port's recomplauncher.json, relative to the repo - a fixed part of every release since 2026-11-24, RecompLauncher's agent guide)"
}
$Toolkit = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Repo = if ($C.Repo) { $C.Repo } else { Split-Path -Parent (Split-Path -Parent ([IO.Path]::GetFullPath($Config))) }
$Private = Join-Path (Split-Path -Parent $Repo) "private"
$Shared = Join-Path (Split-Path -Parent $Toolkit) "private"
$Deliver = if ($Out) { $Out } else { Join-Path $Private "deliver" }
$StageRoot = if ($Out) { $Out } else { Join-Path $Private "release" }
$Cache = Join-Path $Shared "deps\release_cache"
$ExeName = $C.ExeName
$Exe = Join-Path $Deliver $ExeName
$utf8 = New-Object Text.UTF8Encoding($false)
. (Join-Path $Toolkit "tools\lock.ps1")

if (-not (Test-Path -LiteralPath $Notes)) { throw "no release notes at $Notes" }
$D = Join-Path $Deliver $(if ($Folder) { $Folder } else { "v$Version" })
if (Test-Path -LiteralPath $D) { throw "$D exists - a release folder is written once" }

# the exe must be this version's (the payload trailer: magic, offset, 32-byte version)
$fs = [IO.File]::OpenRead($Exe)
try {
    $t = New-Object byte[] 48
    [void]$fs.Seek(-48, [IO.SeekOrigin]::End)
    [void]$fs.Read($t, 0, 48)
} finally { $fs.Close() }
if ([Text.Encoding]::ASCII.GetString($t, 0, 8) -ne "XRPAYLD1") { throw "$Exe has no payload trailer" }
$exeVer = [Text.Encoding]::ASCII.GetString($t, 16, 32).TrimEnd([char]0)
if ($exeVer -ne $Version) { throw "$Exe is version '$exeVer', not '$Version' - run port_release.ps1 -Version $Version" }

# the release's payload stage (newest of this version): licences + the pinned toolchain
$stage = Get-ChildItem $StageRoot -Directory | Where-Object { $_.Name -like "*_$Version" } | Sort-Object Name | Select-Object -Last 1
if (-not $stage) { throw "no release stage for $Version in $StageRoot" }
$P = Join-Path $stage.FullName "payload"
# toolchain.txt format 2: "bundle <zip under tools\> <sha256> <size> <folder in the zip> <target under tools\>"
$tc = @{}
foreach ($l in Get-Content (Join-Path $P "toolchain.txt")) {
    $f = $l -split "`t"
    if ($f[0] -eq "format") { $tc["format"] = $f[1] }
    if ($f[0] -eq "bundle" -and $f.Count -ge 6) { $tc["zip"] = $f[1]; $tc["sha256"] = $f[2]; $tc["size"] = $f[3] }
    if ($f[0] -eq "url") { throw "toolchain.txt has a url line - the launcher (format 2) refuses it; run port_release.ps1 again" }
}
if ($tc["format"] -ne "2" -or -not $tc["zip"]) { throw "toolchain.txt is not format 2 with a bundle line" }
$tcName = Split-Path -Leaf $tc["zip"]
$tcSub = Split-Path -Parent ($tc["zip"] -replace '/', '\')      # "bundle"
$tcZip = Join-Path $Cache $tcName

$lock = Enter-GlobalLock -Class shared -Who $C.Who -What "release zip $Version"
try {
    if (-not (Test-Path -LiteralPath $tcZip)) { throw "$tcZip is not in the release cache" }
    if ((Get-Item -LiteralPath $tcZip).Length -ne [int64]$tc["size"]) { throw "$tcZip has the wrong size" }
    if ((Get-FileHash -LiteralPath $tcZip).Hash -ne $tc["sha256"].ToUpperInvariant()) { throw "$tcZip has the wrong SHA-256" }

    $Z = Join-Path $D "zip"
    New-Item -ItemType Directory (Join-Path $Z "LICENSES") | Out-Null
    New-Item -ItemType Directory (Join-Path $Z "tools\$tcSub") | Out-Null
    Copy-Item -LiteralPath $Exe -Destination $Z
    Copy-Item (Join-Path $P "licenses\*") (Join-Path $Z "LICENSES")
    Copy-Item -LiteralPath $tcZip -Destination (Join-Path $Z "tools\$tcSub")
    $readme = [IO.File]::ReadAllText((Join-Path $Repo $C.Readme)).Replace("{VERSION}", $Version)
    [IO.File]::WriteAllText((Join-Path $Z "README.txt"), ($readme -replace "`r?`n", "`r`n"), $utf8)
    # recomplauncher.json (the maintainer 2026-11-24: a fixed part of every release, like .recomp.json; RecompLauncher
    # reads it at the top of the zip next to the exe): the port's file, checked - JSON, format 1, at most 1 MiB
    $rl = Join-Path $Repo $C.RecompLauncher
    if ((Get-Item -LiteralPath $rl).Length -gt 1MB) { throw "$rl is larger than 1 MiB" }
    $rlj = Get-Content -LiteralPath $rl -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($rlj.format -ne 1) { throw "$rl has no `"format`": 1" }
    [IO.File]::WriteAllText((Join-Path $Z "recomplauncher.json"), [IO.File]::ReadAllText($rl), $utf8)   # (no BOM)
    Copy-Item -LiteralPath $Notes -Destination (Join-Path $D "release_notes.md")

    # the zip: '/' separators (unpacks the same on Linux); the compiler zip stored as it is (already compressed)
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zipPath = Join-Path $D "$($C.ZipPrefix)-v$Version-win64.zip"
    $zs = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew)
    $za = New-Object IO.Compression.ZipArchive($zs, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($f in Get-ChildItem -LiteralPath $Z -Recurse -File) {
            $rel = $f.FullName.Substring($Z.Length + 1).Replace('\', '/')
            $lvl = if ($f.Extension -eq ".zip") { [IO.Compression.CompressionLevel]::NoCompression } else { [IO.Compression.CompressionLevel]::Optimal }
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $f.FullName, $rel, $lvl)
        }
    } finally { $za.Dispose(); $zs.Close() }

    $sums = foreach ($f in @($zipPath, (Join-Path $Z $ExeName), (Join-Path $Z "tools\$tcSub\$tcName"))) {
        $i = Get-Item -LiteralPath $f
        "{0}  {1}  {2} bytes" -f (Get-FileHash -LiteralPath $f).Hash, $i.Name, $i.Length
    }
    [IO.File]::WriteAllText((Join-Path $D "SHA256SUMS.txt"), (($sums -join "`r`n") + "`r`n"), $utf8)
    $sums | ForEach-Object { Write-Host $_ }
    Write-Host "release folder: $D"
} finally {
    Exit-GlobalLock $lock
}
