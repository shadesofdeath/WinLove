<#
.SYNOPSIS
  D-112 engine check: the Bilesenler rows WinLove reads from an image (drivers, languages, keyboards,
  fonts, system apps) and their removal recipes, on a COPY of the lab install.wim. Reads the
  inventory, removes one row of each kind the way the reference tool did in its own test (research README
  section 8: Welsh, the Gothic keyboard, Gabriola, a system app, a modem driver), verifies files and
  registry, checks that DISM still services the image, then unmounts with DISCARD. Nothing outside
  build\lab is touched; the test ISO is never written.

  Needs an elevated PowerShell:
    powershell -ExecutionPolicy Bypass -File tools\lab_inventory.ps1
  The whole output is also written to build\lab\out\inventory-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [int] $Index = 4,              # Windows 11 Pro in the 25H2 lab image
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$golden = Join-Path $Lab 'setup\sources\install.wim'
$work = Join-Path $Lab 'work\inventory.wim'
$mount = Join-Path $Lab 'mount\inventory'
$out = Join-Path $Lab 'out'
$log = Join-Path $out 'inventory-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $golden)) { throw "Lab image missing ($golden)" }
foreach ($dir in 'work', 'mount\inventory', 'out') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }

$failed = 0
function Say([string] $text) {
    Add-Content -Path $log -Value $text -Encoding UTF8
    Write-Host $text
}
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
    $output | Where-Object { $_ -notmatch '^\s*((mount|discard|commit)\s+)?[\d.]+%\s*$' -and $_.Trim() } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*[\d.]+%)+', '').TrimEnd()) }
    return $code
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}
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
# Writes the recipe of one inventory row to a file; returns its path (or $null).
function Recipe([string] $row) {
    $file = Join-Path $out (($row -replace '[:\\/ ]', '_') + '.json')
    $json = Native { & $Cli inventory-recipe $mount $row }
    if ($LASTEXITCODE -ne 0) { Say ("  no recipe for $row : " + ($json -join ' ')); return $null }
    [System.IO.File]::WriteAllText($file, ($json -join "`n"), (New-Object System.Text.UTF8Encoding($false)))
    Say ("  recipe $row : " + ($json -join ' '))
    return $file
}
function Remove-Row([string] $row) {
    $file = Recipe $row
    if (-not $file) { return $false }
    return (Run @('component', $mount, $file, '--remove', '--verbose')) -eq 0
}

Remove-Item $log -ErrorAction SilentlyContinue
Say "WinLove image inventory check (D-112) - $(Get-Date -Format s)"
Say "copying the lab image (the original stays untouched)..."
Copy-Item $golden $work -Force

$mounted = $false
try {
    $mounted = (Run @('mount', $work, "$Index", $mount)) -eq 0
    Check 'mount' $mounted
    if (-not $mounted) { throw 'the image could not be mounted: nothing else can be checked' }
    $system = Join-Path $mount 'Windows\System32\config\SYSTEM'
    $software = Join-Path $mount 'Windows\System32\config\SOFTWARE'

    # ---- the inventory --------------------------------------------------------------------------
    $summary = Native { & $Cli inventory $mount }
    Say ("  " + ($summary -join ' '))
    Check 'inventory reads drivers, languages, keyboards, fonts, system apps' ($LASTEXITCODE -eq 0 -and ($summary -join ' ') -match 'drivers [1-9]')
    $json = Native { & $Cli inventory $mount --json }
    [System.IO.File]::WriteAllText((Join-Path $out 'inventory.json'), ($json -join "`n"), (New-Object System.Text.UTF8Encoding($false)))

    # ---- language: Welsh (the reference tool: System32\cy-GB, SysWOW64\cy-GB, ink\cy-GB) ------------------
    $cy = Join-Path $mount 'Windows\System32\cy-GB'
    Check 'cy-GB folder is in the image' (Test-Path $cy)
    Check 'remove language cy-GB' (Remove-Row 'language:cy-GB')
    Check 'System32\cy-GB is gone' (-not (Test-Path $cy))
    Check 'SysWOW64\cy-GB is gone' (-not (Test-Path (Join-Path $mount 'Windows\SysWOW64\cy-GB')))

    # ---- keyboard: Gothic (KBDGTHC.DLL + Keyboard Layouts\000C0C00) -----------------------------
    $kbd = Join-Path $mount 'Windows\System32\KBDGTHC.DLL'
    Check 'KBDGTHC.DLL is in the image' (Test-Path $kbd)
    Check 'remove keyboard 000C0C00' (Remove-Row 'keyboard:000C0C00')
    Check 'KBDGTHC.DLL is gone' (-not (Test-Path $kbd))
    Check 'Keyboard Layouts\000C0C00 is gone' (-not (InHive $system 'ControlSet001\Control\Keyboard Layouts\000C0C00' ''))
    Check 'Keyboard Layouts\00000409 stays' (InHive $system 'ControlSet001\Control\Keyboard Layouts\00000409' '')

    # ---- font: Gabriola (Fonts\Gabriola.ttf + the Fonts value) ----------------------------------
    $font = Join-Path $mount 'Windows\Fonts\Gabriola.ttf'
    Check 'Gabriola.ttf is in the image' (Test-Path $font)
    Check 'remove font Gabriola' (Remove-Row 'font:Gabriola')
    Check 'Gabriola.ttf is gone' (-not (Test-Path $font))
    Check 'Fonts value is gone' (-not (InHive $software 'Microsoft\Windows NT\CurrentVersion\Fonts' 'Gabriola (TrueType)'))

    # ---- system app: People experience host (SystemApps folder + Deprovisioned) -----------------
    $apps = (Get-Content (Join-Path $out 'inventory.json') -Raw | ConvertFrom-Json).systemApps
    $people = $apps | Where-Object { $_.name -eq 'Microsoft.Windows.PeopleExperienceHost' } | Select-Object -First 1
    if ($people) {
        $folder = Join-Path $mount $people.folder
        Check 'PeopleExperienceHost folder is in the image' (Test-Path $folder)
        Check 'remove system app PeopleExperienceHost' (Remove-Row 'systemapp:Microsoft.Windows.PeopleExperienceHost')
        Check 'its SystemApps folder is gone' (-not (Test-Path $folder))
        $store = 'Microsoft\Windows\CurrentVersion\Appx\AppxAllUserStore'
        Check 'the family is deprovisioned' (InHive $software ("$store\Deprovisioned\" + $people.family) '')
        Check 'its InboxApplications key is gone' (-not (InHive $software ("$store\InboxApplications\" + $people.fullName) ''))
    } else {
        Say "  (no PeopleExperienceHost in this image)"
    }

    # ---- driver: a modem INF, deep-removed (DriverStore, DRIVERS, WinSxS, INF) -------------------
    $drivers = (Get-Content (Join-Path $out 'inventory.json') -Raw | ConvertFrom-Json).drivers
    $modem = $drivers | Where-Object { $_.inf -eq 'mdm5674a.inf' } | Select-Object -First 1
    if ($modem) {
        $repo = Join-Path $mount ('Windows\System32\DriverStore\FileRepository\' + $modem.package)
        Check 'mdm5674a.inf package is in the DriverStore' (Test-Path $repo)
        Check 'remove driver mdm5674a.inf' (Remove-Row 'driver:mdm5674a.inf')
        Check 'its FileRepository folder is gone' (-not (Test-Path $repo))
        Check 'another modem INF stays' (@($drivers | Where-Object { $_.class -eq $modem.class -and $_.inf -ne 'mdm5674a.inf' } |
            Where-Object { Test-Path (Join-Path $mount ('Windows\System32\DriverStore\FileRepository\' + $_.package)) }).Count -gt 0)
    } else {
        Say "  (no mdm5674a.inf in this image)"
    }

    # ---- a required class is refused --------------------------------------------------------------
    $net = $drivers | Where-Object { $_.classGuid -eq '{4D36E972-E325-11CE-BFC1-08002BE10318}' -and -not $_.classInf } | Select-Object -First 1
    if ($net) {
        Check ('a network driver cannot be removed (' + $net.inf + ')') (-not (Remove-Row ('driver:' + $net.inf)))
    }

    # The image must still be serviceable after all that.
    $packages = Native { & $Cli packages $mount }
    Say ("`n> wlcli packages: exit $LASTEXITCODE, " + @($packages).Count + " line(s)")
    Check 'DISM still lists packages' ($LASTEXITCODE -eq 0 -and @($packages).Count -gt 50)
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
