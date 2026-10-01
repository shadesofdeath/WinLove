<#
.SYNOPSIS
  Engine check for the 2026-10-01 features (D-048 ... D-055) on a real image, in one run:
    drivers in the image (list, add one of this PC's, remove it again), this PC's driver export,
    international settings (read, change the time zone and keyboard, read back), default app
    associations (import), an .appx / .msix provisioned offline (Windows Terminal from
    build\lab\appx, with its dependency), scheduled tasks / hosts / files through the queue,
    and the same queue on a second edition (--also) with the WIM rewritten once.
  Works on a two-edition copy of the lab WIM in build\lab\work (Home + Pro exported), mounted
  into build\lab\mount\features; everything is deleted at the end. The ISO is only read.

  Needs an elevated PowerShell (about 15 minutes, 25 GB free) and:
    build\lab\setup\sources\install.wim  (wlcli extract-all <test.iso> build\lab\setup)
    build\lab\appx\...msix               (winget download Microsoft.WindowsTerminal -d build\lab\appx)
  Optional: -LanguageFolder <Microsoft's Languages and Optional Features media> to add one pack.

    powershell -ExecutionPolicy Bypass -File tools\lab_features.ps1
  The whole output is also written to build\lab\out\features-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [string] $LanguageFolder = '',
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
if (-not (Test-Path $source)) { $source = Join-Path $Lab 'iso\sources\install.wim' }
$work = Join-Path $Lab 'work\features'
$wim = Join-Path $work 'features.wim'
$mount = Join-Path $Lab 'mount\features'
$check = Join-Path $Lab 'mount\features-check'
$log = Join-Path $Lab 'out\features-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $source)) { throw "No lab install.wim: run  wlcli extract-all <test.iso> build\lab\setup  first" }
foreach ($dir in 'out', 'mount') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work, $mount, $check | Out-Null

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
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $output = @(Native { & $Cli @arguments })
    $script:lastExit = $LASTEXITCODE
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export)\s+)?\d+%\s*$' } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*(mount|commit|discard|export)\s+\d+%)+', '').TrimEnd()) }
    Say ("  (exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $output
}
function Json([string[]] $arguments) {
    $text = (Native { & $Cli @arguments } | Out-String)
    $script:jsonExit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0 -or -not $text.Trim()) { return $null }
    # Windows PowerShell 5.1 passes a parsed JSON array down the pipeline as one object, so
    # @(Json ...) of "[]" counted 1. Unroll it here: an empty array gives nothing.
    $parsed = $text | ConvertFrom-Json
    foreach ($item in $parsed) { $item }
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}
function WriteUtf8([string] $path, [string] $text) {
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding $false))
}
function ChangeSet([string] $path, [object[]] $operations) {
    $doc = [ordered]@{ format = 'winlove.changeset'; version = 1; operations = $operations }
    WriteUtf8 $path ($doc | ConvertTo-Json -Depth 5)
}

Say ("=== lab_features " + (Get-Date -Format s))
try {
    # 1. A two-edition WIM of our own: Home (1) and Pro (2).
    Run @('export', $source, '1', $wim) | Out-Null
    Check 'export Home into the lab WIM' ($script:lastExit -eq 0)
    Run @('export', $source, '4', $wim) | Out-Null
    Check 'export Pro as the second edition' ($script:lastExit -eq 0)
    Run @('mount', $wim, '1', $mount) | Out-Null
    Check 'mount edition 1 read-write' ($script:lastExit -eq 0)

    # 2. D-052 drivers: list, add one of this PC's, find it, remove it.
    $before = @(Json @('drivers', $mount, '--json'))
    Check "drivers list reads ($($before.Count) third-party)" ($script:jsonExit -eq 0)
    $hostDrivers = Join-Path $work 'host-drivers'
    Run @('export-host-drivers', $hostDrivers) | Out-Null
    $infs = @(Get-ChildItem $hostDrivers -Recurse -Filter *.inf -ErrorAction SilentlyContinue)
    Check "this PC's drivers exported ($($infs.Count) INF)" ($script:lastExit -eq 0 -and $infs.Count -gt 0)
    if ($infs.Count -gt 0) {
        $inf = ($infs | Sort-Object Length | Select-Object -First 1).FullName
        $cs = Join-Path $work 'driver.json'
        ChangeSet $cs @(@{ kind = 'addDriver'; target = $inf; value = 'test'; risk = 'low' })
        Run @('apply', $cs, $mount) | Out-Null
        Check "one driver added ($([System.IO.Path]::GetFileName($inf)))" ($script:lastExit -eq 0)
        $after = @(Json @('drivers', $mount, '--json'))
        $new = @($after | Where-Object { $before.publishedName -notcontains $_.publishedName })
        Check 'the new driver is listed by DismGetDrivers' ($new.Count -ge 1)
        if ($new.Count -ge 1) {
            Run @('drivers', $mount, "--remove=$($new[0].publishedName)") | Out-Null
            $again = @(Json @('drivers', $mount, '--json'))
            Check "DismRemoveDriver took $($new[0].publishedName) out" ($again.Count -eq $before.Count)
        }
    }

    # 3. D-053 international settings.
    $intl = Json @('intl', $mount, '--json')
    Check "intl read (UI $($intl.ui), zone $($intl.timezone), languages $($intl.languages -join ','))" ($null -ne $intl -and $intl.ui)
    $set = Join-Path $work 'intl.json'
    WriteUtf8 $set '{"timezone":"GMT Standard Time","input":"0409:00000409"}'
    Run @('intl', $mount, "--set=@$set") | Out-Null
    Check 'intl set (dism /Set-TimeZone /Set-InputLocale)' ($script:lastExit -eq 0)
    $intl2 = Json @('intl', $mount, '--json')
    Check "time zone reads back ($($intl2.timezone))" ($intl2.timezone -eq 'GMT Standard Time')
    Check "keyboard reads back ($($intl2.input))" ($intl2.input -like '0409:00000409*')
    if ($LanguageFolder) {
        $packs = @(Get-ChildItem $LanguageFolder -Recurse -Filter 'Microsoft-Windows-Client-Language-Pack_x64_*.cab' | Select-Object -First 1)
        if ($packs.Count -eq 1) {
            $cs = Join-Path $work 'language.json'
            ChangeSet $cs @(@{ kind = 'addPackage'; target = $packs[0].FullName; value = 'language'; risk = 'low' })
            Run @('apply', $cs, $mount) | Out-Null
            $intl3 = Json @('intl', $mount, '--json')
            Check "language pack added ($($packs[0].Name)): languages $($intl3.languages -join ',')" ($intl3.languages.Count -gt $intl.languages.Count)
        }
    } else {
        Say 'SKIP  language pack (no -LanguageFolder)'
    }

    # 4. D-054 default app associations.
    $xml = Join-Path $work 'associations.xml'
    WriteUtf8 $xml ('<?xml version="1.0" encoding="UTF-8"?><DefaultAssociations>' +
        '<Association Identifier=".txt" ProgId="txtfilelegacy" ApplicationName="Notepad" />' +
        '<Association Identifier="http" ProgId="MSEdgeHTM" ApplicationName="Microsoft Edge" /></DefaultAssociations>')
    Run @('associations', $mount, $xml) | Out-Null
    Check 'associations imported (dism /Import-DefaultAppAssociations)' ($script:lastExit -eq 0)
    $oem = Join-Path $mount 'Windows\System32\OEMDefaultAssociations.xml'
    Say ("INFO  OEMDefaultAssociations.xml in the image: " + (Test-Path $oem))

    # 5. D-050 an app provisioned offline.
    $package = Get-ChildItem (Join-Path $Lab 'appx') -Filter *.msix -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like '*Terminal*' } | Select-Object -First 1
    if ($package) {
        Run @('appx-add', $mount, $package.FullName) | Out-Null
        Check "provisioned $($package.Name)" ($script:lastExit -eq 0)
        $apps = @(Json @('appx', $mount, '--json'))
        Check 'DISM lists Microsoft.WindowsTerminal as provisioned' (@($apps | Where-Object { $_.packageName -like 'Microsoft.WindowsTerminal_*' }).Count -ge 1)
    } else {
        Say 'SKIP  appx (no Terminal package in build\lab\appx: winget download Microsoft.WindowsTerminal -d build\lab\appx)'
    }

    # 6. D-048 / D-049 / D-051 through the queue, then commit and the same on edition 2 (D-055).
    $payload = Join-Path $work 'payload'
    New-Item -ItemType Directory -Force $payload | Out-Null
    WriteUtf8 (Join-Path $payload 'readme.txt') 'WinLove lab payload'
    $task = '\Microsoft\Windows\Autochk\Proxy'
    $cs = Join-Path $work 'queue.json'
    ChangeSet $cs @(
        @{ kind = 'setTaskState'; target = $task; value = 'disabled'; risk = 'low' },
        @{ kind = 'setHosts'; target = 'telemetry'; value = "0.0.0.0 vortex.data.microsoft.com`r`n0.0.0.0 watson.telemetry.microsoft.com`r`n"; risk = 'medium' },
        @{ kind = 'copyTree'; target = 'Tools\payload'; value = $payload; risk = 'low' })
    Run @('apply', $cs, $mount, '--commit', '--also=2', "--wim=$wim") | Out-Null
    Check 'queue applied to edition 1, saved, then to edition 2 (--also)' ($script:lastExit -eq 0)

    # What edition 2 has now (read-only mount).
    Run @('mount', $wim, '2', $check, '--readonly') | Out-Null
    Check 'edition 2 mounts read-only' ($script:lastExit -eq 0)
    $tasks = Join-Path $check 'Windows\Setup\Scripts\WinLove\tasks.cmd'
    Check 'edition 2: tasks.cmd switches Autochk\Proxy off' ((Test-Path $tasks) -and ((Get-Content $tasks -Raw) -like '*Autochk\Proxy*'))
    $setupComplete = Join-Path $check 'Windows\Setup\Scripts\SetupComplete.cmd'
    Check 'edition 2: SetupComplete.cmd calls tasks.cmd' ((Test-Path $setupComplete) -and ((Get-Content $setupComplete -Raw) -like '*tasks.cmd*'))
    $hosts = Get-Content (Join-Path $check 'Windows\System32\drivers\etc\hosts') -Raw
    Check 'edition 2: hosts has the WinLove telemetry section' ($hosts -like '*# >>> WinLove: telemetry*vortex.data.microsoft.com*')
    Check 'edition 2: Tools\payload\readme.txt' (Test-Path (Join-Path $check 'Tools\payload\readme.txt'))
    Run @('unmount', $check, '--discard') | Out-Null
    Check 'edition 2 unmounted' ($script:lastExit -eq 0)

    Run @('mount', $wim, '1', $check, '--readonly') | Out-Null
    Check 'edition 1: the committed hosts section is there' ((Get-Content (Join-Path $check 'Windows\System32\drivers\etc\hosts') -Raw) -like '*WinLove: telemetry*')
    if ($package) {
        $appx = Join-Path $check 'Program Files\WindowsApps'
        Check 'edition 1: Terminal files in WindowsApps' (@(Get-ChildItem $appx -Filter 'Microsoft.WindowsTerminal*' -ErrorAction SilentlyContinue).Count -ge 1)
    }
    Run @('unmount', $check, '--discard') | Out-Null
    $info = Json @('info', $wim, '--json')
    Check "the WIM still has 2 editions ($($info.install.imageCount))" ($info.install.imageCount -eq 2)
} finally {
    Native { & $Cli cleanup } | Out-Null
    foreach ($m in $mount, $check) {
        if (Test-Path (Join-Path $m 'Windows')) { Native { & $Cli unmount $m --discard } | Out-Null }
    }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    Check 'lab WIM and work files deleted' (-not (Test-Path $wim))
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
