<#
.SYNOPSIS
  Engine check for D-067 (the GitHub tweak set): EVERY setting of resources\catalog\settings.json is
  queued with its non-default state (toggle: the other state; dropdown / radio: the recommended
  option, else the last one; text / picture settings are left out) and applied to a copy of a real
  image through wlcli apply (no commit). Then:
    - every registry write is read back from the image with the engine's offline reader
      (wlcli reg-check, offreg.dll) - all must be "in-image";
    - a sample of values of every type and hive is read again with reg.exe (independent of WinLove);
    - service start types, a key deletion, the first-logon file;
    - a service the image does not have is skipped without leaving a key behind.
  Works on a copy of edition 4 in build\lab\work\settings067, mounted into build\lab\mount\settings067;
  everything is discarded at the end. The ISO and build\lab\setup are only read.

    powershell -ExecutionPolicy Bypass -File tools\lab_settings_d067.ps1      (elevated, ~5 min)

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe",
    [string] $Catalog = "$PSScriptRoot\..\resources\catalog\settings.json",
    [int] $Edition = 4
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
$work = Join-Path $Lab 'work\settings067'
$mount = Join-Path $Lab 'mount\settings067'
$log = Join-Path $Lab 'out\settings-d067-test.log'
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $source)) { throw "no $source (run tools\lab_setup.ps1 first)" }
foreach ($dir in 'out', 'mount') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
if (Test-Path (Join-Path $mount 'Windows')) { & $Cli unmount $mount --discard | Out-Null }
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
function Run([string[]] $arguments, [switch] $Quiet) {
    Say ("`n> wlcli " + ($arguments -join ' '))
    $output = @(Native { & $Cli @arguments })
    $script:lastExit = $LASTEXITCODE
    if (-not $Quiet) {
        $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export)\s+)?\d+%\s*$' } | ForEach-Object { Say ("  " + $_.TrimEnd()) }
    }
    Say ("  (exit $script:lastExit)")
    return $output
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }
function RegValue([string] $key, [string] $name) {
    $out = Native { reg.exe query $key /v $name }
    $line = $out | Where-Object { $_ -match ('^\s+' + [regex]::Escape($name) + '\s+REG_') } | Select-Object -First 1
    if (-not $line) { return $null }
    return ($line -replace ('^\s+' + [regex]::Escape($name) + '\s+'), '').Trim()
}
function RegDefault([string] $key) {
    $out = Native { reg.exe query $key /ve }
    $line = $out | Where-Object { $_ -match '^\s+\(\S+\)\s+REG_' } | Select-Object -First 1
    if (-not $line) { return $null }
    return ($line -replace '^\s+\(\S+\)\s+', '').Trim()
}
function KeyExists([string] $key) { Native { reg.exe query $key } | Out-Null; return $LASTEXITCODE -eq 0 }

Say ("=== lab_settings_d067 " + (Get-Date -Format s))

# ---- the queue: every setting in its non-default state -----------------------------------------
$cat = Get-Content $Catalog -Raw -Encoding UTF8 | ConvertFrom-Json
$ops = [ordered]@{}     # target|kind -> op (last one wins, as in the queue)
$regLines = [ordered]@{} # target -> .reg lines (same order, for reg-check)
$services = [ordered]@{}
$settingsUsed = 0
foreach ($s in $cat.settings) {
    $control = if ($s.control) { $s.control } else { 'toggle' }
    if ($control -eq 'text' -or $control -eq 'file') { continue }
    $writes = @(); $svcs = @()
    if ($control -eq 'toggle') {
        if ($s.writes) { $writes = @($s.writes) }
        if ($s.services) { $svcs = @($s.services) }
    } else {
        $options = @($s.options | Where-Object { $_.writes -or $_.services })
        $pick = $null
        if ($s.recommended) { $pick = $options | Where-Object { $_.id -eq $s.recommended } | Select-Object -First 1 }
        if (-not $pick) { $pick = $options[-1] }
        if ($pick.writes) { $writes = @($pick.writes) }
        if ($pick.services) { $svcs = @($pick.services) }
    }
    $kind = if ($s.apply -eq 'firstLogon') { 'setRegistryFirstLogon' } else { 'setRegistryValue' }
    foreach ($w in $writes) {
        $name = if ($null -ne $w.name) { [string]$w.name } else { '' }
        if ($w.value -eq '[-]') { $target = $w.key + '\\::' }
        elseif ($w.value -eq '[+]') { $target = $w.key + '\::' }
        else { $target = $w.key + '::' + $name }
        $ops["$kind|$target"] = [ordered]@{ kind = $kind; target = $target; value = [string]$w.value; risk = 'low' }
        if ($w.value -eq '[-]') { $regLines[$target] = @("[-$($w.key)]") }
        else {
            $quoted = if ($name -eq '') { '@' } else { '"' + ($name -replace '\\', '\\' -replace '"', '\"') + '"' }
            $regLines[$target] = @("[$($w.key)]", ($quoted + '=' + $w.value))
        }
    }
    foreach ($v in $svcs) { $services[$v.name] = $v.start }
    $settingsUsed++
}
foreach ($name in $services.Keys) {
    $ops["setServiceStart|$name"] = [ordered]@{ kind = 'setServiceStart'; target = $name; value = $services[$name]; risk = 'low' }
}
# Not in any image: must be skipped, not created.
$ops['setServiceStart|WinLoveNoSuchSvc'] = [ordered]@{ kind = 'setServiceStart'; target = 'WinLoveNoSuchSvc'; value = 'disabled'; risk = 'low' }
Say ("settings used: $settingsUsed, operations: $($ops.Count) (registry $($regLines.Count), services $($services.Count + 1))")

$doc = [ordered]@{ format = 'winlove.changeset'; version = 1; operations = @($ops.Values) }
[System.IO.File]::WriteAllText("$work\changes.json", ($doc | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding $false))
$reg = @('Windows Registry Editor Version 5.00', '')
foreach ($lines in $regLines.Values) { $reg += $lines; $reg += '' }
[System.IO.File]::WriteAllLines("$work\expected.reg", $reg, (New-Object System.Text.UnicodeEncoding $false, $true))

$loaded = @()
try {
    Run @('export', $source, "$Edition", "$work\image.wim") | Out-Null
    Run @('mount', "$work\image.wim", '1', $mount) | Out-Null
    Check "mount edition $Edition" ($script:lastExit -eq 0)

    $applyOut = Run @('apply', "$work\changes.json", $mount, '--verbose') -Quiet
    $applyOut | Where-Object { $_ -match 'error|fail|skipped|refus' } | ForEach-Object { Say ("  " + $_.TrimEnd()) }
    Check "apply: $($ops.Count) operation(s), none failed" ($script:lastExit -eq 0)

    # ---- the engine's reader: every write is in the image ----
    $checkOut = Run @('reg-check', "$work\expected.reg", $mount, '--json') -Quiet
    $json = ($checkOut | Where-Object { $_ -notmatch '^\s*\d+%\s*$' }) -join "`n"
    # Windows PowerShell 5.1 hands a JSON array over as one object: unroll it.
    $rows = @(($json | ConvertFrom-Json) | ForEach-Object { $_ })
    $notHeld = @($rows | Where-Object { $_.status -ne 'in-image' })
    foreach ($r in $notHeld) { Say ("  not in image: " + $r.target + " = " + $r.value + "  (" + $r.status + ": " + $r.current + ")") }
    Check "reg-check: $($rows.Count - $notHeld.Count) / $($rows.Count) writes in the image" ($rows.Count -gt 300 -and $notHeld.Count -eq 0)

    # ---- reg.exe, independent of WinLove ----
    Native { reg.exe load HKLM\WL67_SYS "$mount\Windows\System32\config\SYSTEM" } | Out-Null; $loaded += 'HKLM\WL67_SYS'
    Native { reg.exe load HKLM\WL67_SOFT "$mount\Windows\System32\config\SOFTWARE" } | Out-Null; $loaded += 'HKLM\WL67_SOFT'
    Native { reg.exe load HKLM\WL67_USER "$mount\Users\Default\NTUSER.DAT" } | Out-Null; $loaded += 'HKLM\WL67_USER'
    Native { reg.exe load HKLM\WL67_CLS "$mount\Users\Default\AppData\Local\Microsoft\Windows\UsrClass.dat" } | Out-Null; $loaded += 'HKLM\WL67_CLS'
    $sys = 'HKLM\WL67_SYS\ControlSet001'; $soft = 'HKLM\WL67_SOFT'; $user = 'HKLM\WL67_USER'
    Check 'DWORD policy (Click to Do)' ((RegValue "$soft\Policies\Microsoft\Windows\WindowsAI" 'DisableClickToDo') -eq 'REG_DWORD    0x1')
    Check 'REG_SZ in SOFTWARE (ConsentStore generativeAI = Deny)' ((RegValue "$soft\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\generativeAI" 'Value') -eq 'REG_SZ    Deny')
    Check 'REG_BINARY in SYSTEM (Caps Lock scancode map)' ((RegValue "$sys\Control\Keyboard Layout" 'Scancode Map') -eq 'REG_BINARY    00000000000000000200000000003A0000000000')
    Check 'REG_QWORD (RealTimeIsUniversal)' ((RegValue "$sys\Control\TimeZoneInformation" 'RealTimeIsUniversal') -eq 'REG_QWORD    0x1')
    Check 'DWORD 0xffffffff (NetworkThrottlingIndex)' ((RegValue "$soft\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile" 'NetworkThrottlingIndex') -eq 'REG_DWORD    0xffffffff')
    Check 'REG_EXPAND_SZ in HKCR (.reg ShellNew ItemName)' ((RegValue "$soft\Classes\.reg\ShellNew" 'ItemName') -eq 'REG_EXPAND_SZ    @%SystemRoot%\regedit.exe,-309')
    Check 'default value with quotes (msi Extract command)' ((RegDefault "$soft\Classes\Msi.Package\shell\Extract\Command") -eq 'REG_SZ    msiexec.exe /a "%1" /qb TARGETDIR="%1 extracted"')
    Check 'empty REG_SZ (blocked shell extension)' ((RegValue "$soft\Microsoft\Windows\CurrentVersion\Shell Extensions\Blocked" '{7AD84985-87B4-4a16-BE58-8B72A5B390F7}') -eq 'REG_SZ')
    Check 'WOW6432Node twin written' ($null -ne (RegValue "$soft\WOW6432Node\Microsoft\Windows\CurrentVersion\Shell Extensions\Blocked" '{7AD84985-87B4-4a16-BE58-8B72A5B390F7}'))
    Check 'key deleted (removable drives twice)' (-not (KeyExists "$soft\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace\DelegateFolders\{F5FB2C77-0E2F-4A16-A381-3E560C68BC83}"))
    Check 'default user DWORD (Alt+Tab filter)' ((RegValue "$user\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced" 'MultiTaskingAltTabFilter') -eq 'REG_DWORD    0x3')
    Check 'default user key with %% in its name (Console\%%Startup, conhost)' ((RegValue "$user\Console\%%Startup" 'DelegationConsole') -eq 'REG_SZ    {B23D10C0-E52E-411E-9D5B-C09FDF709C7D}')
    Check 'default user policy (MotW SaveZoneInformation)' ((RegValue "$user\Software\Microsoft\Windows\CurrentVersion\Policies\Attachments" 'SaveZoneInformation') -eq 'REG_DWORD    0x1')
    Check 'UsrClass.dat (folder type discovery)' ((RegValue 'HKLM\WL67_CLS\Local Settings\Software\Microsoft\Windows\Shell\Bags\AllFolders\Shell' 'FolderType') -eq 'REG_SZ    NotSpecified')
    Check 'system sounds: scheme .None' ((RegDefault "$user\AppEvents\Schemes") -eq 'REG_SZ    .None')
    Check 'SettingsPageVisibility' ($null -ne (RegValue "$soft\Microsoft\Windows\CurrentVersion\Policies\Explorer" 'SettingsPageVisibility'))
    foreach ($pair in @(@('PcaSvc', '0x4'), @('dmwappushservice', '0x4'), @('XblAuthManager', '0x4'), @('Spooler', '0x4'), @('SysMain', '0x4'))) {
        Check "service $($pair[0]) Start = $($pair[1])" ((RegValue "$sys\Services\$($pair[0])" 'Start') -eq "REG_DWORD    $($pair[1])")
    }
    if (KeyExists "$sys\Services\WSAIFabricSvc") {
        Check 'WSAIFabricSvc Start = 3 (manual, recommended)' ((RegValue "$sys\Services\WSAIFabricSvc" 'Start') -eq 'REG_DWORD    0x3')
    } else {
        Say 'INFO  WSAIFabricSvc is not in this image (skipped by the engine)'
    }
    Check 'a service the image does not have leaves no key' (-not (KeyExists "$sys\Services\WinLoveNoSuchSvc"))
    Check 'the skip is logged' (($applyOut -join "`n") -match 'WinLoveNoSuchSvc')
    foreach ($hive in $loaded) { Native { reg.exe unload $hive } | Out-Null }
    $loaded = @()

    $firstLogon = Get-ChildItem "$mount\Windows\Setup\Scripts" -Recurse -Filter 'firstlogon-user.reg' -ErrorAction SilentlyContinue | Select-Object -First 1
    $setupReg = Get-ChildItem "$mount\Windows\Setup\Scripts" -Recurse -Filter 'setupcomplete.reg' -ErrorAction SilentlyContinue | Select-Object -First 1
    Check 'first-logon (user) file has the Alt+Tab filter' ($firstLogon -and ((Get-Content $firstLogon.FullName -Raw) -match 'MultiTaskingAltTabFilter'))
    Check 'setup-complete (machine) file has Smart App Control' ($setupReg -and ((Get-Content $setupReg.FullName -Raw) -match 'VerifiedAndReputablePolicyState'))
} catch {
    Say ("ERROR  " + $_.Exception.Message + " (line " + $_.InvocationInfo.ScriptLineNumber + ")")
    $failed++
} finally {
    foreach ($hive in $loaded) { Native { reg.exe unload $hive } | Out-Null }
    if (Test-Path (Join-Path $mount 'Windows')) { Run @('unmount', $mount, '--discard') | Out-Null }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
