<#
.SYNOPSIS
  Engine check for the D-062 settings on a real image: page file (REG_MULTI_SZ), desktop icon
  size, Control Panel view, Defender switched off (services, policies, tray, right-click scan),
  Spotlight kept off the desktop (first logon). The queue goes through wlcli apply (no commit);
  the values are read back from the image's hives with reg.exe, and the first-logon file is checked.
  Works on a copy of edition 4 in build\lab\work\settings, mounted into build\lab\mount\settings;
  everything is discarded at the end. The ISO and build\lab\setup are only read.

    powershell -ExecutionPolicy Bypass -File tools\lab_settings_d062.ps1      (elevated, ~5 min)

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
$work = Join-Path $Lab 'work\settings'
$mount = Join-Path $Lab 'mount\settings'
$log = Join-Path $Lab 'out\settings-d062-test.log'
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
    $output = @(Native { & $Cli @arguments })
    $script:lastExit = $LASTEXITCODE
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export)\s+)?\d+%\s*$' } | ForEach-Object { Say ("  " + $_.TrimEnd()) }
    Say ("  (exit $script:lastExit)")
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }
function RegValue([string] $key, [string] $name) {
    $out = Native { reg.exe query $key /v $name }
    $line = $out | Where-Object { $_ -match ('^\s+' + [regex]::Escape($name) + '\s+REG_') } | Select-Object -First 1
    if (-not $line) { return $null }
    return ($line -replace ('^\s+' + [regex]::Escape($name) + '\s+REG_\w+\s*'), '').Trim()
}
function Op([string] $kind, [string] $target, [string] $value) { return [ordered]@{ kind = $kind; target = $target; value = $value; risk = 'low' } }
function MultiSz([string] $text) {
    $bytes = [System.Text.Encoding]::Unicode.GetBytes($text + [char]0 + [char]0)
    return 'hex(7):' + (($bytes | ForEach-Object { '{0:x2}' -f $_ }) -join ',')
}

Say ("=== lab_settings_d062 " + (Get-Date -Format s))
$loaded = @()
try {
    Run @('export', $source, '4', "$work\pro.wim")
    Run @('mount', "$work\pro.wim", '1', $mount)
    Check 'mount Pro' ($script:lastExit -eq 0)

    $mm = 'HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management'
    $ops = @(
        (Op 'setRegistryValue' "$mm::PagingFiles" (MultiSz 'D:\pagefile.sys 2048 4096')),
        (Op 'setRegistryFirstLogon' 'HKCU\Software\Microsoft\Windows\Shell\Bags\1\Desktop::IconSize' 'dword:00000020'),
        (Op 'setRegistryFirstLogon' 'HKCU\Software\Microsoft\Windows\Shell\Bags\1\Desktop::Mode' 'dword:00000001'),
        (Op 'setRegistryValue' 'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\ControlPanel::StartupPage' 'dword:00000001'),
        (Op 'setRegistryValue' 'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\ControlPanel::AllItemsIconView' 'dword:00000001'),
        (Op 'setRegistryValue' 'HKLM\SOFTWARE\Policies\Microsoft\Windows Defender::DisableAntiSpyware' 'dword:00000001'),
        (Op 'setRegistryValue' 'HKLM\SOFTWARE\Policies\Microsoft\Windows Defender\Real-Time Protection::DisableRealtimeMonitoring' 'dword:00000001'),
        (Op 'setRegistryValue' 'HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run::SecurityHealth' '-'),
        (Op 'setRegistryValue' 'HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Blocked::{09A47860-11B0-4DA5-AFA5-26D86198A780}' '""'),
        (Op 'setServiceStart' 'WinDefend' 'disabled'), (Op 'setServiceStart' 'WdNisSvc' 'disabled'), (Op 'setServiceStart' 'WdNisDrv' 'disabled'),
        (Op 'setServiceStart' 'WdFilter' 'disabled'), (Op 'setServiceStart' 'WdBoot' 'disabled'), (Op 'setServiceStart' 'Sense' 'disabled'),
        (Op 'setRegistryFirstLogon' 'HKCU\Software\Policies\Microsoft\Windows\CloudContent::DisableSpotlightCollectionOnDesktop' 'dword:00000001')
    )
    $doc = [ordered]@{ format = 'winlove.changeset'; version = 1; operations = $ops }
    [System.IO.File]::WriteAllText("$work\settings.json", ($doc | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding $false))
    Run @('apply', "$work\settings.json", $mount)
    Check "apply: $($ops.Count) operation(s), none failed" ($script:lastExit -eq 0)

    Native { reg.exe load HKLM\WL_SYS "$mount\Windows\System32\config\SYSTEM" } | Out-Null; $loaded += 'HKLM\WL_SYS'
    Native { reg.exe load HKLM\WL_SOFT "$mount\Windows\System32\config\SOFTWARE" } | Out-Null; $loaded += 'HKLM\WL_SOFT'
    Native { reg.exe load HKLM\WL_USER "$mount\Users\Default\NTUSER.DAT" } | Out-Null; $loaded += 'HKLM\WL_USER'
    $paging = RegValue 'HKLM\WL_SYS\ControlSet001\Control\Session Manager\Memory Management' 'PagingFiles'
    Check "PagingFiles = '$paging' (REG_MULTI_SZ)" ($paging -eq 'D:\pagefile.sys 2048 4096')
    Check 'desktop icon size 32 in the default user' ((RegValue 'HKLM\WL_USER\Software\Microsoft\Windows\Shell\Bags\1\Desktop' 'IconSize') -eq '0x20')
    Check 'Control Panel opens on small icons' ((RegValue 'HKLM\WL_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\ControlPanel' 'AllItemsIconView') -eq '0x1')
    foreach ($svc in 'WinDefend', 'WdNisSvc', 'WdNisDrv', 'WdFilter', 'WdBoot', 'Sense') {
        Check "$svc Start = 4" ((RegValue "HKLM\WL_SYS\ControlSet001\Services\$svc" 'Start') -eq '0x4')
    }
    Check 'policy DisableAntiSpyware' ((RegValue 'HKLM\WL_SOFT\Policies\Microsoft\Windows Defender' 'DisableAntiSpyware') -eq '0x1')
    Check 'tray icon (Run SecurityHealth) gone' ($null -eq (RegValue 'HKLM\WL_SOFT\Microsoft\Windows\CurrentVersion\Run' 'SecurityHealth'))
    Check 'right-click scan blocked' ($null -ne (RegValue 'HKLM\WL_SOFT\Microsoft\Windows\CurrentVersion\Shell Extensions\Blocked' '{09A47860-11B0-4DA5-AFA5-26D86198A780}'))
    Check 'Spotlight off the desktop (default user)' ((RegValue 'HKLM\WL_USER\Software\Policies\Microsoft\Windows\CloudContent' 'DisableSpotlightCollectionOnDesktop') -eq '0x1')
    foreach ($hive in $loaded) { Native { reg.exe unload $hive } | Out-Null }
    $loaded = @()
    $firstLogon = Get-ChildItem "$mount\Windows\Setup\Scripts" -Recurse -Filter 'firstlogon-user.reg' -ErrorAction SilentlyContinue | Select-Object -First 1
    Check 'first-logon file has the icon size and Spotlight' ($firstLogon -and ((Get-Content $firstLogon.FullName -Raw) -match 'IconSize') -and ((Get-Content $firstLogon.FullName -Raw) -match 'DisableSpotlightCollectionOnDesktop'))
} catch {
    Say ("ERROR  " + $_.Exception.Message + " (line " + $_.InvocationInfo.ScriptLineNumber + ")")
    $failed++
} finally {
    foreach ($hive in $loaded) { Native { reg.exe unload $hive } | Out-Null }
    if (Test-Path (Join-Path $mount 'Windows')) { Run @('unmount', $mount, '--discard') }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
