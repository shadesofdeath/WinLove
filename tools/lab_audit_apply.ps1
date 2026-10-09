<#
.SYNOPSIS
  Engine check for two fixes of the 2026-10-09 audit (docs/AUDIT-2026-10.md), on a real image, no VM:
    A4  a preset's app removal names another version of the app (another build): the Applier finds
        the image's version by package family and removes it - before, "not found" counted as done;
    A7  a step that fails holds the image: "wlcli apply --commit" does not save it, it stays mounted
        (before, whatever ran was committed).
  A COPY of one edition of build\lab\setup is exported, mounted, changed, then unmounted with DISCARD
  and deleted. Nothing outside build\lab is touched.

  Needs an elevated PowerShell (about 5 minutes):
    powershell -ExecutionPolicy Bypass -File tools\lab_audit_apply.ps1 [-Cli <wlcli.exe>]
  Output also in build\lab\out\audit-apply-test.log. Keep this file plain ASCII.
#>
param(
    [int] $Index = 4,
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$golden = Join-Path $Lab 'setup\sources\install.wim'
$work = Join-Path $Lab 'work\audit-apply.wim'
$mount = Join-Path $Lab 'mount\audit-apply'
$changes = Join-Path $Lab 'work\audit-apply.json'
$log = Join-Path $Lab 'out\audit-apply-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $golden)) { throw "Lab image missing: $golden" }
foreach ($dir in 'work', 'mount', 'out') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
$failures = 0
function Say([string] $text) { Write-Host $text; Add-Content -Path $log -Value $text -Encoding UTF8 }
function Check([bool] $ok, [string] $what) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failures++ }
}
function Invoke-Wl([string[]] $arguments) {
    Say ("> wlcli " + ($arguments -join ' '))
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue' # wlcli's stderr must not stop the script (NativeCommandError)
    $out = & $Cli @arguments 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    foreach ($line in $out) { Say "    $line" }
    Say "  (exit $code)"
    return [pscustomobject]@{ Code = $code; Text = ($out -join "`n") }
}
Set-Content -Path $log -Value ("=== lab_audit_apply " + (Get-Date -Format s)) -Encoding UTF8

if (Test-Path $mount) { [void] (Invoke-Wl @('unmount', $mount, '--discard')) }
Remove-Item -Force -ErrorAction SilentlyContinue $work
$r = Invoke-Wl @('export', $golden, "$Index", $work)
Check ($r.Code -eq 0) "edition $Index exported"
$r = Invoke-Wl @('mount', $work, '1', $mount)
Check ($r.Code -eq 0) 'mounted'

try {
    $apps = (Invoke-Wl @('appx', $mount)).Text
    $candidates = 'Microsoft.BingWeather', 'Microsoft.GetHelp', 'Microsoft.WindowsFeedbackHub', 'Microsoft.BingNews'
    $real = $null
    foreach ($name in $candidates) {
        $m = [regex]::Match($apps, [regex]::Escape($name) + '_[^\s]+')
        if ($m.Success) { $real = $m.Value; break }
    }
    Check ($null -ne $real) "an app to remove: $real"
    $parts = $real -split '_'
    $other = '{0}_9.9.9.9_{1}_{2}_{3}' -f $parts[0], $parts[2], $parts[3], $parts[4]
    Say "the preset's name (another build): $other"
    $doc = @{
        format = 'winlove.changeset'; version = 1
        operations = @(
            @{ kind = 'removeAppx'; target = $other; risk = 'low' },
            @{ kind = 'enableFeature'; target = 'WinLove-Audit-NoSuchFeature'; risk = 'low' })
    }
    [System.IO.File]::WriteAllText($changes, ($doc | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding($false)))

    $r = Invoke-Wl @('apply', $changes, $mount, '--commit')
    Check ($r.Text -match 'another version of the app|removeAppx') 'the app step ran'
    Check ($r.Text -match '1 failed') 'the missing feature failed (on purpose)'
    Check ($r.Text -match 'commit: held') 'A7: a failed step holds the image (not saved)'
    $mounts = (Invoke-Wl @('mounts')).Text
    Check ($mounts -match [regex]::Escape($mount)) 'A7: the image is still mounted'
    $after = (Invoke-Wl @('appx', $mount)).Text
    Check (-not ($after -match [regex]::Escape($parts[0] + '_'))) "A4: $($parts[0]) is gone (found by its family)"
} finally {
    [void] (Invoke-Wl @('unmount', $mount, '--discard'))
    Remove-Item -Force -ErrorAction SilentlyContinue $work, $changes
}
if ($failures -eq 0) { Say '=== ALL PASSED' } else { Say "=== $failures FAILED" }
exit $failures
