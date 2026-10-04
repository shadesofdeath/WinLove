<#
.SYNOPSIS
  Engine check for D-063 (Microsoft Defender removed from the root) on a real image:
    1. the "defender-full" recipe through wlcli apply (packages, files, service keys, the
       Windows Security app natively, right-click scan, tray icon);
    2. what is left: files, service keys, provisioned app, CBS packages;
    3. the component store: dism /Cleanup-Image /ScanHealth;
    4. (-WithLcu) the cumulative update added afterwards: does it install, and what of Defender
       does it bring back.
  A copy of edition 4 in build\lab\work\defender, mounted into build\lab\mount\defender and
  discarded at the end. The ISO and build\lab\setup are only read.

    powershell -ExecutionPolicy Bypass -File tools\lab_defender.ps1 [-WithLcu]   (elevated)

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [switch] $WithLcu,
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
$work = Join-Path $Lab 'work\defender'
$mount = Join-Path $Lab 'mount\defender'
$log = Join-Path $Lab 'out\defender-test.log'
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
foreach ($dir in 'out', 'mount') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work, $mount | Out-Null
Remove-Item $log -ErrorAction SilentlyContinue

$failed = 0
function Say([string] $text) { Add-Content -Path $log -Value $text -Encoding UTF8; Write-Host $text }
function Native([scriptblock] $command) {
    $previous = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $output = & $command 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = $previous
    return $output
}
function Run([string[]] $arguments) {
    Say ("`n> wlcli " + ($arguments -join ' '))
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $output = @(Native { & $Cli @arguments })
    $script:lastExit = $LASTEXITCODE
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export)\s+)?\d+%\s*$' } | ForEach-Object { Say ("  " + $_.TrimEnd()) }
    Say ("  (exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $output
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }
function WriteUtf8([string] $path, [string] $text) { [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding $false)) }
function ScanHealth([string] $when) {
    Say "`n> dism /Cleanup-Image /ScanHealth ($when)"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $out = Native { dism.exe /English "/Image:$mount" /Cleanup-Image /ScanHealth }
    $line = ($out | Where-Object { $_ -match 'component store' } | Select-Object -First 1)
    Say ("  " + $line + " (" + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $line
}
$defenderFiles = @('Program Files\Windows Defender\MsMpEng.exe', 'Program Files\Windows Defender\MpSvc.dll',
                   'Windows\System32\drivers\WdFilter.sys', 'Windows\System32\drivers\WdBoot.sys')
function Leftovers() {
    $files = @($defenderFiles | Where-Object { Test-Path (Join-Path $mount $_) })
    Native { reg.exe load HKLM\WL_DEF "$mount\Windows\System32\config\SYSTEM" } | Out-Null
    $services = @(foreach ($name in 'WinDefend', 'WdNisSvc', 'WdNisDrv', 'WdFilter', 'WdBoot', 'Sense') {
        Native { reg.exe query "HKLM\WL_DEF\ControlSet001\Services\$name" } | Out-Null
        if ($LASTEXITCODE -eq 0) { $name }
    })
    Native { reg.exe unload HKLM\WL_DEF } | Out-Null
    return @{ files = $files; services = $services }
}

# The catalog entry "defender-full" (resources\catalog\components.json) as the queue carries it.
$svc = 'HKLM\SYSTEM\CurrentControlSet\Services\'
$cls = 'HKLM\SOFTWARE\Classes\'
$recipe = [ordered]@{
    title = 'Microsoft Defender (completely)'
    packages = @('Windows-Defender-AM-Default-Definitions-OptionalWrapper-Package', 'Windows-Defender-AM-Default-Definitions-Package', 'Windows-Defender-Group-Policy-Package')
    paths = @('Program Files\Windows Defender', 'Program Files (x86)\Windows Defender', 'Program Files\Windows Defender Advanced Threat Protection',
              'ProgramData\Microsoft\Windows Defender', 'ProgramData\Microsoft\Windows Defender Advanced Threat Protection',
              'Windows\System32\drivers\WdBoot.sys', 'Windows\System32\drivers\WdFilter.sys', 'Windows\System32\drivers\WdNisDrv.sys')
    appx = @('Microsoft.SecHealthUI')
    registry = @(
        @('WinDefend', 'WdNisSvc', 'WdNisDrv', 'WdFilter', 'WdBoot', 'Sense' | ForEach-Object { [ordered]@{ target = "$svc$_\::"; value = '[-]' } }) +
        @('*', 'Directory', 'Drive' | ForEach-Object { [ordered]@{ target = "$cls$_\shellex\ContextMenuHandlers\EPP\::"; value = '[-]' } }) +
        @([ordered]@{ target = 'HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run::SecurityHealth'; value = '-' })
    )
}
$recipeJson = $recipe | ConvertTo-Json -Depth 5 -Compress

Say ("=== lab_defender " + (Get-Date -Format s) + $(if ($WithLcu) { ' (with LCU)' } else { '' }))
try {
    Run @('export', $source, '4', "$work\pro.wim") | Out-Null
    Run @('mount', "$work\pro.wim", '1', $mount) | Out-Null
    Check 'mount Pro' ($script:lastExit -eq 0)
    $before = Leftovers
    Check "before: Defender files ($($before.files.Count)) and services ($($before.services.Count)) are there" ($before.files.Count -eq 4 -and $before.services.Count -eq 6)

    $doc = [ordered]@{ format = 'winlove.changeset'; version = 1; operations = @([ordered]@{ kind = 'removeComponent'; target = 'defender-full'; value = $recipeJson; risk = 'high' }) }
    WriteUtf8 "$work\defender.json" ($doc | ConvertTo-Json -Depth 6)
    Run @('apply', "$work\defender.json", $mount) | Out-Null
    Check 'apply: defender-full ran without a failed step' ($script:lastExit -eq 0)

    $after = Leftovers
    Check "files gone (left: $($after.files -join ', '))" ($after.files.Count -eq 0)
    Check "service keys gone (left: $($after.services -join ', '))" ($after.services.Count -eq 0)
    $apps = @(Native { & $Cli appx $mount })
    Check 'Windows Security app no longer provisioned' (@($apps | Where-Object { $_ -match 'SecHealthUI' }).Count -eq 0)
    $cbs = @(Native { & $Cli cbs $mount 'Defender' })
    Say ($cbs -join "`n")
    Check 'definitions and group policy packages gone' (@($cbs | Where-Object { $_ -match '0x70\s+Windows-Defender-(AM-Default|Group-Policy)' }).Count -eq 0)
    $health = ScanHealth 'after removal'
    Check "component store after removal: $health" ($health -match 'No component store corruption')

    if ($WithLcu) {
        $lcu = Get-ChildItem (Join-Path $Lab 'updates') -Filter 'windows11.0-kb5129195-x64*.msu' | Select-Object -First 1
        if (-not $lcu) { throw 'No LCU in build\lab\updates (wlcli catalog 26200.8037 --download=build\lab\updates)' }
        $doc = [ordered]@{ format = 'winlove.changeset'; version = 1; operations = @([ordered]@{ kind = 'addPackage'; target = $lcu.FullName; value = 'lcu'; risk = 'low' }) }
        WriteUtf8 "$work\lcu.json" ($doc | ConvertTo-Json -Depth 5)
        Run @('apply', "$work\lcu.json", $mount) | Out-Null
        Check "cumulative update $($lcu.Name.Substring(0, 22)) installs after the removal" ($script:lastExit -eq 0)
        $again = Leftovers
        Say "  after the LCU: files back: $($again.files -join ', '); service keys back: $($again.services -join ', ')"
        $apps = @(Native { & $Cli appx $mount })
        Say "  Windows Security app provisioned again: $(@($apps | Where-Object { $_ -match 'SecHealthUI' }).Count -gt 0)"
        $health = ScanHealth 'after the LCU'
        Check "component store after the LCU: $health" ($health -match 'No component store corruption')
    }
} catch {
    Say ("ERROR  " + $_.Exception.Message + " (line " + $_.InvocationInfo.ScriptLineNumber + ")")
    $failed++
} finally {
    if (Test-Path (Join-Path $mount 'Windows')) { Run @('unmount', $mount, '--discard') | Out-Null }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
