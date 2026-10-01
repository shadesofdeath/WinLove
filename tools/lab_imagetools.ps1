<#
.SYNOPSIS
  Engine check for the Images page tools (D-058) on the real lab image: SHA-256 of the test ISO,
  copy an edition inside a WIM, recompress (XPRESS -> ESD -> LZX), split to SWM and join back,
  add editions from the ISO and from an SWM, capture a folder (admin). Every result is checked
  with wlcli info / verify. Works in build\lab\work\tools; deletes it at the end. The ISO is only read.

    powershell -ExecutionPolicy Bypass -File tools\lab_imagetools.ps1 [-Iso <test.iso>]
  Needs an elevated PowerShell for the capture (the rest does not); about 15 minutes, 30 GB free.
  Log: build\lab\out\imagetools-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [string] $Iso = 'C:\Users\shades\Downloads\Win11_25H2_Turkish_x64_v2.iso',
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-release\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
if (-not (Test-Path $source)) { $source = Join-Path $Lab 'iso\sources\install.wim' }
$work = Join-Path $Lab 'work\tools'
$log = Join-Path $Lab 'out\imagetools-test.log'
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1 -Config Release" }
if (-not (Test-Path $source)) { throw "No lab install.wim: run  wlcli extract-all <test.iso> build\lab\setup  first" }
New-Item -ItemType Directory -Force (Join-Path $Lab 'out') | Out-Null
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work | Out-Null

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
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*([a-z0-9-]+\s+)?\d+%\s*$' } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*[a-z0-9-]+\s+\d+%)+', '').TrimEnd()) }
    Say ("  (exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $output
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }
function Info([string] $file) {
    # An empty object when the file is missing or unreadable: the check after it fails, the run goes on.
    if (-not (Test-Path $file)) { return [pscustomobject]@{ install = [pscustomobject]@{ imageCount = 0; images = @() } } }
    try { return ((Native { & $Cli info $file --json }) -join "`n") | ConvertFrom-Json }
    catch { return [pscustomobject]@{ install = [pscustomobject]@{ imageCount = 0; images = @() } } }
}
function Mb([string] $file) { if (Test-Path $file) { [math]::Round((Get-Item $file).Length / 1MB) } else { 0 } }

Say ("=== lab_imagetools " + (Get-Date -Format s))
try {
    # 1. SHA-256 of the ISO, against itself and against a wrong value.
    if (Test-Path $Iso) {
        $out = Run @('hash', $Iso)
        $hash = (($out | Where-Object { $_ -match 'SHA-256 ([0-9a-f]{64})' }) -replace '.*SHA-256 ([0-9a-f]{64}).*', '$1') | Select-Object -First 1
        Check "ISO SHA-256 computed ($hash)" ($script:lastExit -eq 0 -and $hash.Length -eq 64)
        Run @('hash', $Iso, "--expect=SHA256: $($hash.ToUpper())") | Out-Null
        Check 'the same hash (upper case, with a prefix) matches' ($script:lastExit -eq 0)
        Run @('hash', $Iso, ('--expect=' + ('0' * 64))) | Out-Null
        Check 'another hash does not match (exit 3)' ($script:lastExit -eq 3)
    } else {
        Say "SKIP  ISO hash (no $Iso)"
    }

    # 2. A two-edition WIM, then a copy of an edition inside it.
    $two = Join-Path $work 'two.wim'
    Run @('export', $source, '1', $two) | Out-Null
    Run @('export', $source, '4', $two) | Out-Null
    $before = Mb $two
    Run @('duplicate', $two, '2', 'Windows 11 Pro - Oyun') | Out-Null
    $i = Info $two
    Check "copy of edition 2 is index 3 '$($i.install.images[2].name)'" ($script:lastExit -eq 0 -and $i.install.imageCount -eq 3 -and $i.install.images[2].name -eq 'Windows 11 Pro - Oyun')
    Check "the copy shares every file (WIM $before -> $(Mb $two) MB)" ((Mb $two) - $before -lt 100)

    # 3. Recompress: XPRESS, then ESD (solid LZMS, new name), then LZX back.
    Run @('recompress', $two, '--compress=xpress') | Out-Null
    $i = Info $two
    Check "XPRESS: $($i.install.compression), $($i.install.imageCount) editions, $(Mb $two) MB" ($script:lastExit -eq 0 -and $i.install.compression -eq 'XPRESS' -and $i.install.imageCount -eq 3)
    Run @('recompress', $two, '--compress=esd') | Out-Null
    $esd = Join-Path $work 'two.esd'
    $i = Info $esd
    Check "ESD: two.esd $($i.install.compression) solid=$($i.install.solid), $(Mb $esd) MB, two.wim gone" ($script:lastExit -eq 0 -and $i.install.solid -and -not (Test-Path $two))
    Run @('recompress', $esd, '--compress=lzx') | Out-Null
    $i = Info $two
    Check "LZX again: two.wim $($i.install.compression), $($i.install.imageCount) editions, $(Mb $two) MB" ($script:lastExit -eq 0 -and $i.install.compression -eq 'LZX' -and $i.install.imageCount -eq 3)
    Run @('recompress', $two, '--compress=lzx') | Out-Null
    Check 'the same compression again is refused' ($script:lastExit -ne 0)

    # 4. Split into SWM parts and join them back.
    $parts = Join-Path $work 'parts\install.swm'
    New-Item -ItemType Directory -Force (Split-Path $parts) | Out-Null
    Run @('swm-split', $two, $parts, '--size-mb=1500') | Out-Null
    $count = @(Get-ChildItem (Split-Path $parts) -Filter '*.swm').Count
    Check "split into $count parts" ($script:lastExit -eq 0 -and $count -ge 3)
    $merged = Join-Path $work 'merged.wim'
    Run @('swm-merge', $parts, $merged) | Out-Null
    $i = Info $merged
    Check "SWM joined: $($i.install.imageCount) editions, $(Mb $merged) MB" ($script:lastExit -eq 0 -and $i.install.imageCount -eq 3)
    Run @('verify', $merged) | Out-Null
    Check 'the joined WIM verifies' ($script:lastExit -eq 0)

    # 5. Editions added from the ISO and from the SWM.
    if (Test-Path $Iso) {
        Run @('append', $Iso, $two, '--index=3') | Out-Null
        $i = Info $two
        Check "from the ISO: index 4 '$($i.install.images[3].name)'" ($script:lastExit -eq 0 -and $i.install.imageCount -eq 4)
    }
    Run @('append', $parts, $two, '--index=1') | Out-Null
    $i = Info $two
    Check "from the SWM: $($i.install.imageCount) editions" ($script:lastExit -eq 0 -and $i.install.imageCount -ge 4)
    Run @('verify', $two) | Out-Null
    Check 'the WIM with added editions verifies' ($script:lastExit -eq 0)

    # 6. Capture a folder (admin).
    if ($admin) {
        $folder = Join-Path $work 'capture'
        New-Item -ItemType Directory -Force (Join-Path $folder 'Docs\Sub') | Out-Null
        Set-Content -Path (Join-Path $folder 'readme.txt') -Value 'WinLove capture test' -Encoding ASCII
        Copy-Item C:\Windows\System32\notepad.exe (Join-Path $folder 'Docs\Sub\notepad.exe')
        $cap = Join-Path $work 'capture.wim'
        Run @('capture', $folder, $cap, 'Lab capture') | Out-Null
        $i = Info $cap
        Check "captured: '$($i.install.images[0].name)', $($i.install.images[0].fileCount) files" ($script:lastExit -eq 0 -and $i.install.images[0].name -eq 'Lab capture' -and $i.install.images[0].fileCount -ge 2)
        Run @('capture', $folder, $cap, 'Lab capture 2', '--compress=xpress') | Out-Null
        $i = Info $cap
        Check "a second capture is appended ($($i.install.imageCount) editions)" ($script:lastExit -eq 0 -and $i.install.imageCount -eq 2)
        Run @('verify', $cap) | Out-Null
        Check 'the captured WIM verifies' ($script:lastExit -eq 0)
    } else {
        Say 'SKIP  capture (needs an elevated PowerShell)'
    }
} finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    Check 'work files deleted' (-not (Test-Path $work))
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
