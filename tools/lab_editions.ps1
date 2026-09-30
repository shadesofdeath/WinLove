<#
.SYNOPSIS
  Real-image test of edition removal (P02, core::removeImages). No admin needed.
  Works on a copy of the lab WIM in build\lab\edition-test and deletes the copy at the end:
  the lab WIM itself is only read. Needs about 14 GB free while it runs.

  Steps: bad index and "all editions" are refused; one edition is removed; then every
  edition but -Keep is removed. After each step the edition list is printed; at the end
  7-Zip (when installed) verifies every stream of the rewritten file.
#>
param(
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe",
    [string] $Keep = 'Windows 11 Pro',
    [switch] $KeepCopy
)
$ErrorActionPreference = 'Stop'

$source = Join-Path $Lab 'iso\sources\install.wim'
if (-not (Test-Path $source)) { throw "Lab WIM not found: run tools\lab_setup.ps1 first" }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }

$dir = Join-Path $Lab 'edition-test'
$wim = Join-Path $dir 'install.wim'
New-Item -ItemType Directory -Force $dir | Out-Null
Write-Host "copy the lab WIM (about 7 GB)..."
Copy-Item $source $wim -Force

# Callers wrap the result in @(): PowerShell unrolls a one-element array on return.
function Get-Editions {
    $info = & $Cli info $wim --json | ConvertFrom-Json
    return $info.install.images
}
function Invoke-Remove([string] $list) {
    & $Cli delete-index $wim $list | Out-Host
    return $LASTEXITCODE
}

$failed = $false
try {
    $before = @(Get-Editions)
    Write-Host ("editions: " + $before.Count)
    if ($before.Count -lt 3) { throw "the lab WIM needs at least 3 editions" }

    if ((Invoke-Remove '99') -eq 0) { throw "a missing index was accepted" }
    $all = ($before | ForEach-Object { $_.index }) -join ','
    if ((Invoke-Remove $all) -eq 0) { throw "removing every edition was accepted" }
    if (@(Get-Editions).Count -ne $before.Count) { throw "a refused removal changed the file" }

    # One edition: the first that is not the one to keep.
    $victim = $before | Where-Object { $_.name -ne $Keep } | Select-Object -First 1
    if ((Invoke-Remove ([string] $victim.index)) -ne 0) { throw "removing one edition failed" }
    $after = @(Get-Editions)
    if ($after.Count -ne $before.Count - 1) { throw "expected one edition less" }
    if ($after | Where-Object { $_.name -eq $victim.name }) { throw "the removed edition is still listed" }

    # Every edition but the one to keep.
    $rest = ($after | Where-Object { $_.name -ne $Keep } | ForEach-Object { $_.index }) -join ','
    if ((Invoke-Remove $rest) -ne 0) { throw "removing the other editions failed" }
    $final = @(Get-Editions)
    if ($final.Count -ne 1 -or $final[0].name -ne $Keep -or $final[0].index -ne 1) {
        throw "expected only '$Keep' as index 1"
    }
    & $Cli info $wim
    # (-Filter 'install.wim.*' would match install.wim itself.)
    if (Get-ChildItem $dir | Where-Object { $_.Name -ne 'install.wim' }) { throw "a .new / .old file was left behind" }

    $sevenZip = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
    if (Test-Path $sevenZip) {
        Write-Host "7-Zip: testing every stream of the rewritten WIM..."
        & $sevenZip t $wim | Select-Object -Last 8
        if ($LASTEXITCODE -ne 0) { throw "7-Zip found errors in the rewritten WIM" }
    } else {
        Write-Host "7-Zip not installed: stream test skipped"
    }
    Write-Host "`nPASS  edition removal"
} catch {
    $failed = $true
    Write-Host "`nFAIL  $_"
} finally {
    if (-not $KeepCopy) {
        Remove-Item $dir -Recurse -Force -Confirm:$false
    }
}
if ($failed) { exit 1 }
