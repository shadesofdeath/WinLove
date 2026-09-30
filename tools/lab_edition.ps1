<#
.SYNOPSIS
  Engine check for the edition change (P02 "Surumu yukselt", core/image/dism/Edition) on a real
  image, without a VM: mounts a COPY of the lab install.wim, reads the current and target
  editions, changes Home to Pro with dism.exe /Set-Edition through wlcli, commits, then checks
  what the WIM says afterwards, renames the edition and verifies every stream of the file.
  Nothing outside build\lab is touched; the test ISO is never written. The copy is deleted.

  Needs an elevated PowerShell (about 10 minutes, 8 GB free):
    powershell -ExecutionPolicy Bypass -File tools\lab_edition.ps1
  The whole output is also written to build\lab\out\edition-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [int] $Index = 1,                      # Windows 11 Home in the 25H2 test ISO
    [string] $Target = 'Professional',
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$golden = Join-Path $Lab 'iso\sources\install.wim'
$work = Join-Path $Lab 'work\edition.wim'
$mount = Join-Path $Lab 'mount\edition'
$log = Join-Path $Lab 'out\edition-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $golden)) { throw "Lab image missing: run tools\lab_setup.ps1 first ($golden)" }
foreach ($dir in 'work', 'mount\edition', 'out') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }

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
    $output = Native { & $Cli @arguments }
    $code = $LASTEXITCODE
    # Progress lines (mount 12%, 35.0%) would bury the rest of the log.
    $output | Where-Object { $_ -notmatch '^\s*((mount|discard|commit|verify)\s+)?[\d.]+%\s*$' -and $_.Trim() } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*((verify)\s+)?[\d.]+%)+', '').TrimEnd()) }
    Say ("  (exit $code, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $code
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}
function Editions {
    $text = (Native { & $Cli edition $mount --json } | Out-String)
    if ($LASTEXITCODE -ne 0 -or -not $text.Trim()) { return $null }
    return $text | ConvertFrom-Json
}
function ImageOf([string] $wim, [int] $index) {
    $info = (Native { & $Cli info $wim --json } | Out-String) | ConvertFrom-Json
    return @($info.install.images) | Where-Object { $_.index -eq $index } | Select-Object -First 1
}

Remove-Item $log -ErrorAction SilentlyContinue
Say "WinLove edition change check - $(Get-Date -Format s)"
Say "copying the lab image (the original stays untouched)..."
Copy-Item $golden $work -Force

$mounted = $false
$committed = $false
try {
    $before = ImageOf $work $Index
    Say ("image ${Index}: " + $before.name + " (" + $before.editionId + ")")

    $mounted = (Run @('mount', $work, "$Index", $mount)) -eq 0
    Check 'mount' $mounted
    if (-not $mounted) { throw 'the image could not be mounted: nothing else can be checked' }

    $editions = Editions
    Check 'editions are read' ($null -ne $editions)
    if ($null -eq $editions) { throw 'dism.exe did not list the editions' }
    Say ("  current: " + $editions.current)
    Say ("  targets: " + (@($editions.targets) -join ', '))
    Check 'the current edition matches the WIM' ($editions.current -eq $before.editionId)
    Check "$Target is a target edition" (@($editions.targets) -contains $Target)

    Check 'an unknown edition is refused' ((Run @('edition', $mount, '--set=NoSuchEdition')) -ne 0)
    Check 'a command line is not an edition' ((Run @('edition', $mount, '--set=Pro /Foo')) -ne 0)
    $still = Editions
    Check 'a refused change leaves the edition alone' ($null -ne $still -and $still.current -eq $editions.current)

    Check "set edition $Target" ((Run @('edition', $mount, "--set=$Target", '--verbose')) -eq 0)
    $after = Editions
    Check 'the image now says it is the target edition' ($null -ne $after -and $after.current -eq $Target)
    if ($null -ne $after) { Say ("  targets now: " + (@($after.targets) -join ', ')) }

    # The image must still be serviceable after that.
    $packages = Native { & $Cli packages $mount }
    Say ("`n> wlcli packages: exit $LASTEXITCODE, " + @($packages).Count + " line(s)")
    Check 'DISM still lists packages' ($LASTEXITCODE -eq 0 -and @($packages).Count -gt 50)

    $committed = (Run @('unmount', $mount, '--commit')) -eq 0
    $mounted = -not $committed
    Check 'unmount (commit)' $committed
    if ($committed) {
        $saved = ImageOf $work $Index
        Say ("  after the commit: name '" + $saved.name + "', edition id '" + $saved.editionId + "'")
        Check 'the WIM records the new edition id' ($saved.editionId -eq $Target)
        Check 'rename the edition' ((Run @('set-info', $work, "$Index", 'Windows 11 Pro', 'Windows 11 Pro', "--flags=$Target")) -eq 0)
        $renamed = ImageOf $work $Index
        Check 'the WIM has the new name' ($renamed.name -eq 'Windows 11 Pro' -and $renamed.displayName -eq 'Windows 11 Pro')
        Check 'every stream of the committed WIM is sound' ((Run @('verify', $work)) -eq 0)
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
