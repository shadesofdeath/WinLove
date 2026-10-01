<#
.SYNOPSIS
  Engine check for WLM (D-057) on a real image: a WIM packed with `wlcli wlm-pack` is unpacked
  again (every stream's SHA-1 checked by wlcli), the result verified, mounted read-only with DISM,
  looked into (kernel, registry hives, an edition DISM can read) and unmounted; then exported to LZX
  by wimgapi. Without -Wlm the lab WIM's Pro edition is exported and packed first (about an hour).
  Works in build\lab\work\wlm and build\lab\mount\wlm; deletes what it made. The ISO is only read.

    powershell -ExecutionPolicy Bypass -File tools\lab_wlm.ps1 [-Wlm build\lab\compress\pro.wlm]
  Needs an elevated PowerShell (the DISM mount). Log: build\lab\out\wlm-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [string] $Wlm = '',
    [int] $Index = 4,
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-release\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$work = Join-Path $Lab 'work\wlm'
$mount = Join-Path $Lab 'mount\wlm'
$log = Join-Path $Lab 'out\wlm-test.log'
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1 -Config Release" }
foreach ($dir in 'out', 'mount') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work, $mount | Out-Null

$failed = 0
function Say([string] $text) { Add-Content -Path $log -Value $text -Encoding UTF8; Write-Host $text }
function Native([scriptblock] $command) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $command 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = $previous
    return $output
}
function Run([string[]] $arguments) {
    Say ("`n> wlcli " + ($arguments -join ' '))
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $output = @(Native { & $Cli @arguments })
    $script:lastExit = $LASTEXITCODE
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export|pack|unpack|verify)\s+)?\d+%\s*$' } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*(mount|commit|discard|export|pack|unpack|verify)\s+\d+%)+', '').TrimEnd()) }
    Say ("  (exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $output
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }

Say ("=== lab_wlm " + (Get-Date -Format s))
try {
    if (-not $Wlm) {
        $source = Join-Path $Lab 'setup\sources\install.wim'
        if (-not (Test-Path $source)) { throw "No lab install.wim: run  wlcli extract-all <test.iso> build\lab\setup  first" }
        $one = Join-Path $work 'edition.wim'
        Run @('export', $source, "$Index", $one) | Out-Null
        Check "export edition $Index" ($script:lastExit -eq 0)
        $Wlm = Join-Path $work 'edition.wlm'
        Run @('wlm-pack', $one, $Wlm) | Out-Null
        Check 'wlm-pack' ($script:lastExit -eq 0)
    }
    $Wlm = [System.IO.Path]::GetFullPath($Wlm)
    Run @('wlm-info', $Wlm) | Out-Null
    Check "wlm-info reads $([System.IO.Path]::GetFileName($Wlm)) ($([math]::Round((Get-Item $Wlm).Length / 1GB, 3)) GB)" ($script:lastExit -eq 0)

    $back = Join-Path $work 'unpacked.wim'
    Run @('wlm-unpack', $Wlm, $back) | Out-Null
    Check 'wlm-unpack: every stream matched its SHA-1' ($script:lastExit -eq 0)
    Run @('verify', $back) | Out-Null
    Check 'the unpacked WIM verifies (wlcli verify)' ($script:lastExit -eq 0)
    $info = (Native { & $Cli info $back --json }) -join "`n" | ConvertFrom-Json
    Check "the unpacked WIM has $($info.install.imageCount) edition(s): $($info.install.images[0].name)" ($info.install.imageCount -ge 1)
    $dismInfo = Native { dism.exe /English /Get-WimInfo /WimFile:$back /Index:1 }
    Check 'DISM /Get-WimInfo reads it' ([bool]($dismInfo -match 'Name :'))

    Run @('mount', $back, '1', $mount, '--readonly') | Out-Null
    Check 'DISM mounts the unpacked WIM (read-only)' ($script:lastExit -eq 0)
    # Windows PE (WinRE, boot.wim) has no shell, no WinRE of its own and no edition to read.
    $pe = $info.install.images[0].editionId -eq 'WindowsPE'
    $files = @('Windows\System32\ntoskrnl.exe', 'Windows\System32\config\SOFTWARE', 'Windows\System32\config\SYSTEM')
    if (-not $pe) { $files += @('Windows\explorer.exe', 'Windows\System32\Recovery\Winre.wim') }
    foreach ($file in $files) {
        Check "mounted image has $file" (Test-Path (Join-Path $mount $file))
    }
    $winre = Join-Path $mount 'Windows\System32\Recovery\Winre.wim'
    if (Test-Path $winre) {
        # WLM v2 rebuilds the WinRE inside (LZX by wimgapi): it must be sound and still boot index 1.
        Run @('verify', $winre) | Out-Null
        Check 'Winre.wim inside verifies (every stream SHA-1)' ($script:lastExit -eq 0)
        $re = (Native { & $Cli info $winre --json }) -join "`n" | ConvertFrom-Json
        Check "Winre.wim boots index $($re.install.bootIndex) ($($re.install.compression), $([math]::Round((Get-Item $winre).Length / 1MB)) MB)" ($re.install.bootIndex -eq 1)
    }
    if (-not $pe) {
        $edition = Native { dism.exe /English /Image:$mount /Get-CurrentEdition }
        Check ('DISM reads the edition of the mounted image: ' + (($edition | Where-Object { $_ -match 'Current Edition' }) -join ' ')) ([bool]($edition -match 'Current Edition'))
    } else {
        Say 'INFO  Windows PE image: shell, WinRE and edition checks skipped'
    }
    Run @('unmount', $mount, '--discard') | Out-Null
    Check 'unmounted' ($script:lastExit -eq 0)

    $lzx = Join-Path $work 'relzx.wim'
    Run @('export', $back, '1', $lzx, '--compress=max') | Out-Null
    Check "wimgapi exports it to LZX ($([math]::Round((Get-Item $lzx).Length / 1GB, 3)) GB)" ($script:lastExit -eq 0)
} finally {
    if (Test-Path (Join-Path $mount 'Windows')) { Native { & $Cli unmount $mount --discard } | Out-Null }
    Native { & $Cli cleanup } | Out-Null
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    Check 'work files deleted' (-not (Test-Path $work))
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
