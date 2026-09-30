<#
.SYNOPSIS
  Engine check for P07 system components (docs/pages/07-components.md, D-031) on a real image,
  without a VM: mounts a COPY of the lab install.wim, removes OneDrive (hidden CBS package +
  setup file + Run value) and Edge (folder + registry) through wlcli, verifies, then unmounts
  with DISCARD. Nothing outside build\lab is touched; the test ISO is never written.

  Needs an elevated PowerShell:
    powershell -ExecutionPolicy Bypass -File tools\lab_components.ps1
  With -Cleanup it also runs the component store cleanup (/ResetBase) - 5 to 20 minutes.
  The whole output is also written to build\lab\out\components-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [int] $Index = 4,              # Windows 11 Pro in the 25H2 test ISO
    [switch] $Cleanup,
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$fixtures = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\tests\integration\fixtures'))
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
    $output = Native { & $Cli @arguments }
    $code = $LASTEXITCODE
    # Progress lines (mount 12%, 35.0%) would bury the rest of the log.
    $output | Where-Object { $_ -notmatch '^\s*((mount|discard|commit)\s+)?[\d.]+%\s*$' -and $_.Trim() } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*[\d.]+%)+', '').TrimEnd()) }
    return $code
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}
# True when <key>\<value> (value empty: the key itself) exists in a hive file of the image.
function InHive([string] $hiveFile, [string] $key, [string] $value) {
    $name = 'WinLoveLabCheck'
    Native { reg.exe load "HKLM\$name" $hiveFile } | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "could not load $hiveFile" }
    if ($value) { Native { reg.exe query "HKLM\$name\$key" /v $value } | Out-Null }
    else { Native { reg.exe query "HKLM\$name\$key" } | Out-Null }
    $found = $LASTEXITCODE -eq 0
    [gc]::Collect()
    Native { reg.exe unload "HKLM\$name" } | Out-Null
    return $found
}

Remove-Item $log -ErrorAction SilentlyContinue
Say "WinLove system component check - $(Get-Date -Format s)"
Say "copying the lab image (the original stays untouched)..."
Copy-Item $golden $work -Force

$mounted = $false
try {
    $mounted = (Run @('mount', $work, "$Index", $mount)) -eq 0
    Check 'mount' $mounted
    if (-not $mounted) { throw 'the image could not be mounted: nothing else can be checked' }
    $software = Join-Path $mount 'Windows\System32\config\SOFTWARE'
    $defaultUser = Join-Path $mount 'Users\Default\NTUSER.DAT'
    $runKey = 'Software\Microsoft\Windows\CurrentVersion\Run'
    $edgeUninstall = 'WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Microsoft Edge'

    # ---- sizes: single files of a mounted WIM are reparse points of their own kind ----
    $setup = Join-Path $mount 'Windows\System32\OneDriveSetup.exe'
    Check 'OneDriveSetup.exe is in the image' (Test-Path $setup)
    Say ("  attributes of OneDriveSetup.exe: " + (Get-Item $setup -Force).Attributes)
    $probe = Native { & $Cli component $mount (Join-Path $fixtures 'recipe-onedrive.json') }
    Say ("  " + ($probe | Select-Object -First 1))
    Check 'OneDrive setup file has a size' (($probe | Select-Object -First 1) -match ', [1-9]\d* bytes')
    $probe = Native { & $Cli component $mount (Join-Path $fixtures 'recipe-winre.json') }
    Say ("  " + ($probe | Select-Object -First 1))
    Check 'Winre.wim has a size' (($probe | Select-Object -First 1) -match 'present, [1-9]\d* bytes')

    # ---- OneDrive: hidden package, setup file, Run value of the default profile ----
    Check 'default profile starts OneDriveSetup (before)' (InHive $defaultUser $runKey 'OneDriveSetup')
    Run @('cbs', $mount, 'OneDrive-Setup') | Out-Null
    Check 'remove OneDrive' ((Run @('component', $mount, (Join-Path $fixtures 'recipe-onedrive.json'), '--remove', '--verbose')) -eq 0)
    Check 'OneDriveSetup.exe is gone' (-not (Test-Path $setup))
    Check 'Run value is gone from the default profile' (-not (InHive $defaultUser $runKey 'OneDriveSetup'))
    # What the image's registry still lists as installed (state 0x70 = 112).
    $listed = (Native { & $Cli cbs $mount 'OneDrive-Setup' --json } | Out-String)
    $installed = $null
    if ($LASTEXITCODE -eq 0 -and $listed.Trim()) {
        $installed = @($listed | ConvertFrom-Json | Where-Object { $_.state -ge 112 })
        Say ("  packages still installed: " + $installed.Count)
        $installed | ForEach-Object { Say ("    " + $_.identity) }
    }
    Check 'OneDrive packages are out of the component store' ($null -ne $installed -and $installed.Count -eq 0)

    # ---- Edge: folder + registry ----
    $edge = Join-Path $mount 'Program Files (x86)\Microsoft\Edge'
    Check 'Edge folder is in the image' (Test-Path $edge)
    Check 'Edge uninstall entry is in the registry (before)' (InHive $software $edgeUninstall '')
    Check 'remove Edge' ((Run @('component', $mount, (Join-Path $fixtures 'recipe-edge.json'), '--remove', '--verbose')) -eq 0)
    Check 'Edge folder is gone' (-not (Test-Path $edge))
    Check 'Edge uninstall entry is gone' (-not (InHive $software $edgeUninstall ''))
    Check 'WebView2 runtime is still there' (Test-Path (Join-Path $mount 'Program Files (x86)\Microsoft\EdgeWebView'))

    # The image must still be serviceable after all that.
    $packages = Native { & $Cli packages $mount }
    Say ("`n> wlcli packages: exit $LASTEXITCODE, " + @($packages).Count + " line(s)")
    Check 'DISM still lists packages' ($LASTEXITCODE -eq 0 -and @($packages).Count -gt 50)

    if ($Cleanup) {
        Say "`ncomponent store cleanup (/ResetBase) - this takes a while..."
        $watch = [Diagnostics.Stopwatch]::StartNew()
        Check 'store cleanup' ((Run @('store-cleanup', $mount, '--resetbase', '--verbose')) -eq 0)
        Say ("  took " + [int]$watch.Elapsed.TotalSeconds + " s")
        $packages = Native { & $Cli packages $mount }
        Check 'DISM still lists packages after the cleanup' ($LASTEXITCODE -eq 0 -and @($packages).Count -gt 50)
    }
}
catch {
    Say "FAIL  script error: $($_.Exception.Message)"
    $failed++
}
finally {
    if ($mounted) {
        Check 'unmount (discard)' ((Run @('unmount', $mount, '--discard')) -eq 0)
    }
    Remove-Item $work -Force -ErrorAction SilentlyContinue
}
if ($failed) { $summary = "$failed check(s) FAILED" } else { $summary = 'all checks passed' }
Say "`n$summary - log: $log"
exit $failed
