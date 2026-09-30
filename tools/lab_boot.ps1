<#
.SYNOPSIS
  Engine check for the boot image patch (core/image/BootImage: patchBootImage) on the real thing:
  takes sources\boot.wim out of the test ISO into build\lab, writes the Windows 11 requirement
  bypasses into the LabConfig key of Setup's own image (and, with -Driver, adds a driver to it),
  commits, then mounts the result once more and reads the values back, checks that the file
  still has its editions and its boot index, and verifies every stream of it.
  Nothing outside build\lab is touched; the ISO is only read; the copy is deleted.

  Needs an elevated PowerShell (about 5 minutes, 2 GB free):
    powershell -ExecutionPolicy Bypass -File tools\lab_boot.ps1
    powershell -ExecutionPolicy Bypass -File tools\lab_boot.ps1 -Driver D:\drivers\vmd\iaStorVD.inf
  The whole output is also written to build\lab\out\boot-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [string] $Iso = 'C:\Users\shades\Downloads\Win11_25H2_Turkish_x64_v2.iso',
    [string] $Driver = '',
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$work = Join-Path $Lab 'work\boot.wim'
$mount = Join-Path $Lab 'mount\boot'
$log = Join-Path $Lab 'out\boot-test.log'
$values = 'BypassTPMCheck', 'BypassSecureBootCheck', 'BypassRAMCheck', 'BypassCPUCheck', 'BypassStorageCheck'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $Iso)) { throw "Test ISO not found: $Iso" }
if ($Driver -and -not (Test-Path $Driver)) { throw "Driver INF not found: $Driver" }
foreach ($dir in 'work', 'mount\boot', 'out') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }

$failed = 0
function Say([string] $text) {
    Add-Content -Path $log -Value $text -Encoding UTF8
    Write-Host $text
}
# Runs a native command and returns its output lines; the exit code stays in $LASTEXITCODE.
# Windows PowerShell turns stderr lines into error records: under 'Stop' the first one would
# end the script, so the preference is relaxed for the call.
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
    $output | Where-Object { $_ -notmatch '^\s*((mount|discard|commit|verify|boot|extract)\s+)?[\d.]+%\s*$' -and $_.Trim() } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*((verify|boot|extract)\s+)?[\d.]+%)+', '').TrimEnd()) }
    Say ("  (exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $output
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}
function Info {
    $text = (Native { & $Cli info $work --json } | Out-String)
    if ($LASTEXITCODE -ne 0 -or -not $text.Trim()) { return $null }
    return ($text | ConvertFrom-Json).install
}
# The LabConfig values present in the SYSTEM hive of the mounted image.
function LabConfig {
    $name = 'WinLoveLabCheck'
    $hive = Join-Path $mount 'Windows\System32\config\SYSTEM'
    Native { reg.exe load "HKLM\$name" $hive } | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "could not load $hive" }
    $found = @()
    foreach ($value in $values) {
        $lines = Native { reg.exe query "HKLM\$name\Setup\LabConfig" /v $value }
        if ($LASTEXITCODE -eq 0 -and ($lines -match 'REG_DWORD\s+0x1')) { $found += $value }
    }
    [gc]::Collect()
    Native { reg.exe unload "HKLM\$name" } | Out-Null
    return $found
}

Remove-Item $log -ErrorAction SilentlyContinue
Say "WinLove boot image patch check - $(Get-Date -Format s)"
Remove-Item $work -Force -ErrorAction SilentlyContinue
Run @('extract', $Iso, 'sources/boot.wim', $work) | Out-Null
Check 'boot.wim taken out of the ISO' ($script:lastExit -eq 0 -and (Test-Path $work))

$mounted = $false
try {
    $before = Info
    Check 'the boot image is read' ($null -ne $before)
    if ($null -eq $before) { throw 'boot.wim could not be read' }
    $editions = @($before.images).Count
    Say ("  $editions edition(s), boot index " + $before.bootIndex + ", " + (Get-Item $work).Length + " bytes")
    Check 'it has a boot index' ($before.bootIndex -gt 0)

    Run @('boot-patch', $work, $mount) | Out-Null
    Check 'a patch that does nothing is refused' ($script:lastExit -ne 0)
    Run @('boot-patch', $work, $mount, '--bypass=tpm,nonsense') | Out-Null
    Check 'an unknown check is refused' ($script:lastExit -ne 0)

    $arguments = @('boot-patch', $work, $mount, '--bypass=all', '--verbose')
    if ($Driver) { $arguments += "--driver=$Driver" }
    $out = Run $arguments
    Check 'boot image patched and committed' ($script:lastExit -eq 0)
    if ($Driver) { Check 'the driver was added' ([bool]($out -match '1 driver\(s\) added')) }

    $after = Info
    Check 'the file still has its editions' ($null -ne $after -and @($after.images).Count -eq $editions)
    Check 'the boot index is unchanged' ($null -ne $after -and $after.bootIndex -eq $before.bootIndex)

    # Read it back from the file, not from the mount the patch used.
    Run @('mount', $work, [string]$before.bootIndex, $mount) | Out-Null
    $mounted = $script:lastExit -eq 0
    Check 'mount the patched image' $mounted
    if ($mounted) {
        $found = @(LabConfig)
        Say ("  LabConfig: " + ($found -join ', '))
        Check 'all five bypass values are in Setup''s registry' ($found.Count -eq $values.Count)
        Check 'setup.exe is still there' (Test-Path (Join-Path $mount 'setup.exe'))
    }
}
catch {
    Say "FAIL  script error: $($_.Exception.Message)"
    $failed++
}
finally {
    if ($mounted) {
        Run @('unmount', $mount, '--discard') | Out-Null
        Check 'unmount (discard)' ($script:lastExit -eq 0)
    }
}
if (Test-Path $work) {
    Run @('verify', $work) | Out-Null
    Check 'every stream of the patched boot.wim is sound' ($script:lastExit -eq 0)
    Remove-Item $work -Force -ErrorAction SilentlyContinue
}
if ($failed) { $summary = "$failed check(s) FAILED" } else { $summary = 'all checks passed' }
Say "`n$summary - log: $log"
exit $failed
