<#
.SYNOPSIS
  Prepares C:\WinLoveLab (docs/TESTING.md). Safe to re-run: existing files are kept.
  No admin needed. The source ISO is only read, never modified.
#>
param(
    [string] $Iso = 'C:\Users\shades\Downloads\Win11_25H2_Turkish_x64_v2.iso',
    [string] $Lab = 'C:\WinLoveLab',
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'

foreach ($dir in 'iso\sources', 'golden', 'work', 'mount', 'out') {
    New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null
}
if (-not (Test-Path $Iso)) { throw "Test ISO not found: $Iso" }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }

$wim = Join-Path $Lab 'iso\sources\install.wim'
if (Test-Path $wim) {
    Write-Host "ok   $wim already present"
} else {
    Write-Host "copy install.wim out of the ISO (about 7 GB)..."
    & $Cli extract $Iso 'sources/install.wim' $wim
    if ($LASTEXITCODE -ne 0) { throw "extract failed ($LASTEXITCODE)" }
}
& $Cli info $wim
Write-Host "`nLab ready: $Lab"
