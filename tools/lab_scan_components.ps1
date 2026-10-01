<#
.SYNOPSIS
  Survey of what a Windows image carries, for the P07 component catalog (D-059): mounts the lab
  install.wim READ-ONLY and writes, under build\lab\out\scan\<edition>\:
    cbs.txt            every CBS package (hidden ones too) with visibility / state
    optional.txt       optional features + capabilities (wlcli optional-features)
    services.txt       services and start types
    drivers.csv        every inbox driver package: folder, INF, Class, Provider, size
    winsxs.csv         WinSxS component folders summed by name (version / hash dropped)
    tree.csv           size of every folder down to 4 levels under the image root
    files.csv          every file over 1 MB (path, size)
    fonts.csv          Windows\Fonts
    hives\             SOFTWARE, SYSTEM, DRIVERS, DEFAULT, COMPONENTS (copies, for offline reading)
    mum\               Windows\servicing\Packages\*.mum (package manifests)
  then unmounts with DISCARD. Nothing outside build\lab is touched; the ISO is never read.

    powershell -ExecutionPolicy Bypass -File tools\lab_scan_components.ps1 [-Index 4]
  Needs an elevated PowerShell. About 10 minutes.

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [int] $Index = 4,              # Windows 11 Pro in the 25H2 test ISO
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-release\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$wim = Join-Path $Lab 'setup\sources\install.wim'
if (-not (Test-Path $wim)) { $wim = Join-Path $Lab 'iso\sources\install.wim' }
$mount = Join-Path $Lab 'mount\scan'
$out = Join-Path $Lab "out\scan\index$Index"

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1 -Config Release" }
if (-not (Test-Path $wim)) { throw "Lab image missing ($wim)" }
New-Item -ItemType Directory -Force $mount, $out, (Join-Path $out 'hives') | Out-Null
$log = Join-Path $out 'scan.log'
function Say([string] $text) { Add-Content -Path $log -Value $text -Encoding UTF8; Write-Host $text }
function Native([scriptblock] $command) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $command 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = $previous
    return $output
}
function Save([string] $name, [string[]] $arguments) {
    $lines = Native { & $Cli @arguments } | Where-Object { $_ -notmatch '^\s*[\d.]+%\s*$' }
    [System.IO.File]::WriteAllLines((Join-Path $out $name), [string[]]$lines, (New-Object System.Text.UTF8Encoding $false))
    Say "  $name ($($lines.Count) lines, exit $LASTEXITCODE)"
}

Say ("=== scan index $Index " + (Get-Date -Format s) + " ($wim)")
$watch = [Diagnostics.Stopwatch]::StartNew()
Native { & $Cli mount $wim $Index $mount --readonly } | Select-Object -Last 2 | ForEach-Object { Say "  $_" }
if ($LASTEXITCODE -ne 0) { throw "mount failed ($LASTEXITCODE)" }
try {
    Save 'cbs.txt' @('cbs', $mount)
    Save 'optional.txt' @('optional-features', $mount)
    Save 'services.txt' @('services', $mount)

    # Hive copies: read later with offreg, no mount needed.
    foreach ($h in 'SOFTWARE', 'SYSTEM', 'DRIVERS', 'DEFAULT', 'COMPONENTS') {
        $src = Join-Path $mount "Windows\System32\config\$h"
        if (Test-Path $src) { Copy-Item $src (Join-Path $out "hives\$h") -Force }
    }
    Say '  hives copied'
    # Package manifests (.mum, XML): which components and child packages each CBS package carries.
    $mum = Join-Path $out 'mum'
    New-Item -ItemType Directory -Force $mum | Out-Null
    Copy-Item (Join-Path $mount 'Windows\servicing\Packages\*.mum') $mum -Force
    Say "  mum copied ($(@(Get-ChildItem $mum).Count) files)"


    # Sizes (folder tree, WinSxS by component, big files, inbox drivers, fonts): Python walks the
    # mount without following reparse points.
    Native { & python (Join-Path $PSScriptRoot 'scan_image_tree.py') $mount $out } | ForEach-Object { Say $_ }
} finally {
    Native { & $Cli unmount $mount --discard } | Select-Object -Last 1 | ForEach-Object { Say "  $_" }
    Say ("=== done in " + [int]$watch.Elapsed.TotalSeconds + " s (out: $out)")
}
