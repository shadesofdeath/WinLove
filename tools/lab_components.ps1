<#
.SYNOPSIS
  Engine check for P07 system components (docs/pages/07-components.md, D-031) on a real image,
  without a VM: mounts a COPY of the lab install.wim, removes OneDrive (hidden CBS package +
  setup file + Run value) and Edge (folder + registry) through wlcli, verifies, then unmounts
  with DISCARD. Nothing outside build\lab is touched; the test ISO is never written.

  Needs an elevated PowerShell:
    powershell -ExecutionPolicy Bypass -File tools\lab_components.ps1
  With -Cleanup it also runs the component store cleanup (/ResetBase) — 5 to 20 minutes.
  The whole output is also written to build\lab\out\components-test.log.
#>
param(
    [int] $Index = 4,              # Windows 11 Pro in the 25H2 test ISO
    [switch] $Cleanup,
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$fixtures = Join-Path $PSScriptRoot '..\tests\integration\fixtures'
$golden = Join-Path $Lab 'iso\sources\install.wim'
$work = Join-Path $Lab 'work\components.wim'
$mount = Join-Path $Lab 'mount\components'
$log = Join-Path $Lab 'out\components-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $golden)) { throw "Lab image missing: run tools\lab_setup.ps1 first ($golden)" }
foreach ($dir in 'work', 'mount\components', 'out') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }

$failed = 0
function Say([string] $text) { $text | Tee-Object -FilePath $log -Append | Write-Host }
function Run([string[]] $arguments) {
    Say ("`n> wlcli " + ($arguments -join ' '))
    # Windows PowerShell turns a native command's stderr lines into error records: under 'Stop'
    # the first one would end the script.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $Cli @arguments 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    $output | ForEach-Object { Say "  $_" }
    return $code
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}

Remove-Item $log -ErrorAction SilentlyContinue
Say "WinLove system component check — $(Get-Date -Format s)"
Say "copying the lab image (the original stays untouched)..."
Copy-Item $golden $work -Force

$mounted = $false
try {
    Check 'mount' ((Run @('mount', $work, "$Index", $mount)) -eq 0)
    $mounted = $true

    # ---- OneDrive: hidden package, setup file, Run value of the default profile ----
    $setup = Join-Path $mount 'Windows\System32\OneDriveSetup.exe'
    Check 'OneDriveSetup.exe is in the image' (Test-Path $setup)
    Run @('cbs', $mount, 'OneDrive-Setup') | Out-Null
    Check 'remove OneDrive' ((Run @('component', $mount, (Join-Path $fixtures 'recipe-onedrive.json'), '--remove', '--verbose')) -eq 0)
    Check 'OneDriveSetup.exe is gone' (-not (Test-Path $setup))
    $left = (& $Cli cbs $mount 'OneDrive-Setup' --json | Out-String) | ConvertFrom-Json
    $installed = @($left | Where-Object { $_.state -ge 0x70 })
    Say ("  packages still installed: " + $installed.Count)
    $installed | ForEach-Object { Say ("    " + $_.identity) }
    Check 'OneDrive packages are out of the component store' ($installed.Count -eq 0)

    # ---- Edge: folder + registry ----
    $edge = Join-Path $mount 'Program Files (x86)\Microsoft\Edge'
    Check 'Edge folder is in the image' (Test-Path $edge)
    Check 'remove Edge' ((Run @('component', $mount, (Join-Path $fixtures 'recipe-edge.json'), '--remove', '--verbose')) -eq 0)
    Check 'Edge folder is gone' (-not (Test-Path $edge))
    Check 'WebView2 runtime is still there' (Test-Path (Join-Path $mount 'Program Files (x86)\Microsoft\EdgeWebView'))

    # The image must still be serviceable after all that.
    Check 'DISM still lists packages' ((Run @('packages', $mount)) -eq 0)

    if ($Cleanup) {
        Say "`ncomponent store cleanup (/ResetBase) — this takes a while..."
        Check 'store cleanup' ((Run @('store-cleanup', $mount, '--resetbase', '--verbose')) -eq 0)
        Check 'DISM still lists packages after the cleanup' ((Run @('packages', $mount)) -eq 0)
    }
}
finally {
    if ($mounted) {
        Check 'unmount (discard)' ((Run @('unmount', $mount, '--discard')) -eq 0)
    }
    Remove-Item $work -Force -ErrorAction SilentlyContinue
}
Say "`n$(if ($failed) { "$failed check(s) FAILED" } else { 'all checks passed' }) — log: $log"
exit $failed
