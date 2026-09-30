<#
.SYNOPSIS
  Engine check for the native removal of provisioned apps (core/image/dism/Appx: removeAppxNative)
  on a real image, without a VM. DISM refuses Microsoft.SecHealthUI and
  Microsoft.DesktopAppInstaller (0x80073CFA); WinLove then does by hand what DISM does for the
  apps it lets go. The script mounts a COPY of the lab install.wim and
    1. removes an ordinary app with DISM and checks that everything WinLove's recipe names is
       gone - the proof that the recipe describes what DISM really does;
    2. removes another ordinary app natively and checks the same, plus the registry;
    3. removes the two apps DISM refuses, through the DISM-then-native path the app will use;
    4. checks that DISM still lists apps and packages of the image;
  then unmounts with DISCARD. Nothing outside build\lab is touched; the copy is deleted.

  Needs an elevated PowerShell (about 5 minutes, 8 GB free):
    powershell -ExecutionPolicy Bypass -File tools\lab_appx.ps1
  The whole output is also written to build\lab\out\appx-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [int] $Index = 4,              # Windows 11 Pro in the 25H2 test ISO
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$golden = Join-Path $Lab 'iso\sources\install.wim'
$work = Join-Path $Lab 'work\appx.wim'
$mount = Join-Path $Lab 'mount\appx'
$log = Join-Path $Lab 'out\appx-test.log'
$store = 'Microsoft\Windows\CurrentVersion\Appx\AppxAllUserStore'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $golden)) { throw "Lab image missing: run tools\lab_setup.ps1 first ($golden)" }
foreach ($dir in 'work', 'mount\appx', 'out') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }

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
# Runs wlcli, logs what it says, returns its output lines (exit code in $script:lastExit).
function Run([string[]] $arguments) {
    Say ("`n> wlcli " + ($arguments -join ' '))
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $output = @(Native { & $Cli @arguments })
    $script:lastExit = $LASTEXITCODE
    $output | Where-Object { $_ -notmatch '^\s*((mount|discard|commit)\s+)?[\d.]+%\s*$' -and $_.Trim() } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*[\d.]+%)+', '').TrimEnd()) }
    Say ("  (exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $output
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}
# True when the key exists in the SOFTWARE hive of the mounted image.
function InSoftware([string] $key) {
    $name = 'WinLoveLabCheck'
    $hive = Join-Path $mount 'Windows\System32\config\SOFTWARE'
    Native { reg.exe load "HKLM\$name" $hive } | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "could not load $hive" }
    Native { reg.exe query "HKLM\$name\$key" } | Out-Null
    $found = $LASTEXITCODE -eq 0
    [gc]::Collect()
    Native { reg.exe unload "HKLM\$name" } | Out-Null
    return $found
}
# The provisioned apps DISM lists: full package names.
function Apps {
    $text = (Native { & $Cli appx $mount --json } | Out-String)
    if ($LASTEXITCODE -ne 0 -or -not $text.Trim()) { return $null }
    return @(($text | ConvertFrom-Json) | ForEach-Object { $_.packageName })
}
function FamilyOf([string] $package) {
    $parts = $package -split '_'
    return $parts[0] + '_' + $parts[-1]
}
function First([object[]] $apps, [string] $prefix) {
    return $apps | Where-Object { $_ -like "$prefix*" } | Select-Object -First 1
}

Remove-Item $log -ErrorAction SilentlyContinue
Say "WinLove native app removal check - $(Get-Date -Format s)"
Say "copying the lab image (the original stays untouched)..."
Copy-Item $golden $work -Force

$mounted = $false
try {
    Run @('mount', $work, "$Index", $mount) | Out-Null
    $mounted = $script:lastExit -eq 0
    Check 'mount' $mounted
    if (-not $mounted) { throw 'the image could not be mounted: nothing else can be checked' }

    $apps = Apps
    Check 'DISM lists the provisioned apps' ($null -ne $apps -and $apps.Count -gt 10)
    if ($null -eq $apps) { throw 'no app list' }
    Say ("  " + $apps.Count + " provisioned app(s)")

    # 1. An ordinary app through DISM: the recipe must describe what DISM leaves behind.
    $news = First $apps 'Microsoft.BingNews_'
    if ($news) {
        $out = Run @('appx-remove', $mount, $news, '--verbose')
        Check 'DISM removes an ordinary app' ($script:lastExit -eq 0 -and ($out -match 'removed by DISM'))
        Check 'after DISM nothing the recipe names is left' ($out -match 'after: files gone, staged none')
        Check 'DISM marks it deprovisioned' (InSoftware "$store\Deprovisioned\$(FamilyOf $news)")
    } else { Say 'SKIP  no Microsoft.BingNews in this image' }

    # 2. Another ordinary app natively: same end state.
    $weather = First $apps 'Microsoft.BingWeather_'
    if ($weather) {
        $family = FamilyOf $weather
        Check 'the app is staged before' (InSoftware "$store\Staged\$family")
        $out = Run @('appx-remove', $mount, $weather, '--native', '--verbose')
        Check 'native removal of an ordinary app' ($script:lastExit -eq 0 -and ($out -match 'removed natively'))
        Check 'its files and staged packages are gone' ($out -match 'after: files gone, staged none')
        Check 'Applications key is gone' (-not (InSoftware "$store\Applications\$weather"))
        Check 'Staged key is gone' (-not (InSoftware "$store\Staged\$family"))
        Check 'Deprovisioned key is there' (InSoftware "$store\Deprovisioned\$family")
    } else { Say 'SKIP  no Microsoft.BingWeather in this image' }

    # 3. The apps DISM refuses, the way the app will do it: DISM first, natively on 0x80073CFA.
    foreach ($prefix in 'Microsoft.SecHealthUI_', 'Microsoft.DesktopAppInstaller_') {
        $package = First $apps $prefix
        if (-not $package) { Say "SKIP  no $prefix in this image"; continue }
        $family = FamilyOf $package
        $out = Run @('appx-remove', $mount, $package, '--verbose')
        if ($out -match 'removed by DISM') {
            Say "NOTE  DISM removed $prefix itself on this image (no native path needed)"
        } else {
            Check "DISM refuses $prefix" ([bool]($out -match 'DISM refuses it'))
        }
        Check "$prefix removed" ($script:lastExit -eq 0)
        Check "$prefix files and staged packages are gone" ([bool]($out -match 'after: files gone, staged none'))
        Check "$prefix Applications key is gone" (-not (InSoftware "$store\Applications\$package"))
        Check "$prefix is marked deprovisioned" (InSoftware "$store\Deprovisioned\$family")
    }

    # 4. The image must still be serviceable, and DISM must agree that the apps are gone.
    $left = Apps
    Check 'DISM still lists the provisioned apps' ($null -ne $left)
    if ($null -ne $left) {
        Say ("  " + $left.Count + " provisioned app(s) left")
        foreach ($prefix in 'Microsoft.BingNews_', 'Microsoft.BingWeather_', 'Microsoft.SecHealthUI_', 'Microsoft.DesktopAppInstaller_') {
            Check "DISM no longer lists $prefix" (-not (First $left $prefix))
        }
    }
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
        Run @('unmount', $mount, '--discard') | Out-Null
        Check 'unmount (discard)' ($script:lastExit -eq 0)
    }
    Remove-Item $work -Force -ErrorAction SilentlyContinue
}
if ($failed) { $summary = "$failed check(s) FAILED" } else { $summary = 'all checks passed' }
Say "`n$summary - log: $log"
exit $failed
