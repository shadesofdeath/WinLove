# WinLove update guard (D-125): keeps what WinLove's Apply did when a Windows update puts it back.
#
# WinLove writes it into the image with guard.json next to it: the user's choices that ran -
# services' start types, machine-wide registry values, removed apps, scheduled tasks switched off,
# removed components' files. SetupComplete.cmd runs it once with -Register: the task
# "\WinLove\Update guard" (SYSTEM) is created and the first check runs. The task then runs it at
# startup, after Windows Update installed something (System log, WindowsUpdateClient event 19) and
# once a day.
#
# The rule, item by item:
#   as wanted                                  -> nothing
#   different at the first check               -> repaired (Setup's own changes)
#   changed since the last check, an update
#   was installed in between                   -> repaired (the update did it)
#   changed since the last check, no update    -> the user's choice: left alone, no longer guarded
# An app provisioned again is always repaired: only servicing provisions apps.
# Removed components whose files came back are only reported: deleting system files on a running
# Windows is not safe. "mode": "report" in guard.json repairs nothing and reports everything.
#
# Log: guard.log next to this file. To take it away: run this file with -Remove (task + folder).
#
# Keep this file ASCII: Windows PowerShell reads a script without a byte order mark as ANSI.
param([switch] $Register, [switch] $Remove)

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$taskPath = '\WinLove\'
$taskName = 'Update guard'
$logFile = Join-Path $here 'guard.log'
$stateFile = Join-Path $here 'state.json'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Write-Log([string] $text) {
    try {
        if ((Test-Path -LiteralPath $logFile) -and (Get-Item -LiteralPath $logFile).Length -gt 512KB) {
            Move-Item -Force -LiteralPath $logFile -Destination ($logFile + '.old')
        }
        $line = '[{0:yyyy-MM-dd HH:mm:ss}] {1}' -f (Get-Date), $text
        [System.IO.File]::AppendAllText($logFile, $line + "`r`n", $utf8)
    } catch { }
}

if ($Remove) {
    Unregister-ScheduledTask -TaskPath $taskPath -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force -LiteralPath $here -ErrorAction SilentlyContinue
    exit 0
}

if ($Register) {
    $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $self = Join-Path $here 'updateguard.ps1'
    $xml = @"
<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Author>WinLove</Author>
    <Description>WinLove: keeps this Windows' choices when an update puts them back. Log: $logFile</Description>
  </RegistrationInfo>
  <Triggers>
    <BootTrigger>
      <Enabled>true</Enabled>
      <Delay>PT3M</Delay>
    </BootTrigger>
    <EventTrigger>
      <Enabled>true</Enabled>
      <Delay>PT2M</Delay>
      <Subscription>&lt;QueryList&gt;&lt;Query Id="0" Path="System"&gt;&lt;Select Path="System"&gt;*[System[Provider[@Name='Microsoft-Windows-WindowsUpdateClient'] and EventID=19]]&lt;/Select&gt;&lt;/Query&gt;&lt;/QueryList&gt;</Subscription>
    </EventTrigger>
    <CalendarTrigger>
      <Enabled>true</Enabled>
      <StartBoundary>2020-01-01T12:00:00</StartBoundary>
      <ScheduleByDay>
        <DaysInterval>1</DaysInterval>
      </ScheduleByDay>
    </CalendarTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>S-1-5-18</UserId>
      <RunLevel>HighestAvailable</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <StartWhenAvailable>true</StartWhenAvailable>
    <ExecutionTimeLimit>PT30M</ExecutionTimeLimit>
    <Priority>7</Priority>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>$ps</Command>
      <Arguments>-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "$self"</Arguments>
    </Exec>
  </Actions>
</Task>
"@
    try {
        Register-ScheduledTask -Xml $xml -TaskPath $taskPath -TaskName $taskName -Force -ErrorAction Stop | Out-Null
        Write-Log 'task registered'
    } catch {
        Write-Log ('task not registered: ' + $_.Exception.Message)
    }
}

# ---- what to keep, and the last check ---------------------------------------------------------

try {
    $plan = [System.IO.File]::ReadAllText((Join-Path $here 'guard.json'), $utf8) | ConvertFrom-Json
} catch {
    Write-Log ('guard.json unreadable: ' + $_.Exception.Message)
    exit 1
}
$reportOnly = ([string] $plan.mode -eq 'report')
$seen = @{}
$adopted = @{}
$lastRun = $null
if (Test-Path -LiteralPath $stateFile) {
    try {
        $state = [System.IO.File]::ReadAllText($stateFile, $utf8) | ConvertFrom-Json
        $lastRun = [string] $state.lastRun
        if ($state.seen) {
            foreach ($p in $state.seen.PSObject.Properties) { $seen[$p.Name] = [string] $p.Value }
        }
        foreach ($a in @($state.adopted)) {
            if ($a) { $adopted[[string] $a] = $true }
        }
    } catch {
        Write-Log ('state.json unreadable, starting again: ' + $_.Exception.Message)
    }
}
$firstRun = [string]::IsNullOrEmpty($lastRun)
$now = Get-Date

# Was something installed since the last check: Windows Update's "installed" event, or a package
# CBS put in place (Setup log, event 2)?
function Test-UpdateSince([string] $since) {
    if ([string]::IsNullOrEmpty($since)) { return $true }
    try {
        $start = [datetime]::Parse($since, [System.Globalization.CultureInfo]::InvariantCulture,
                                   [System.Globalization.DateTimeStyles]::RoundtripKind)
    } catch {
        return $true
    }
    $wu = Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Microsoft-Windows-WindowsUpdateClient'; Id = 19; StartTime = $start } -MaxEvents 1 -ErrorAction SilentlyContinue
    if ($wu) { return $true }
    $cbs = Get-WinEvent -FilterHashtable @{ LogName = 'Setup'; Id = 2; StartTime = $start } -MaxEvents 1 -ErrorAction SilentlyContinue
    return [bool] $cbs
}
$updated = Test-UpdateSince $lastRun

$script:repaired = 0
$script:reported = 0
$script:released = 0

# One item: what it should be, what it is, how to put it back (see the rule at the top).
function Resolve-Item([string] $id, [string] $wanted, [string] $current, [string] $label, [scriptblock] $repair) {
    if ($current -eq $wanted) {
        $seen[$id] = $current
        return
    }
    if ($adopted.ContainsKey($id)) { return }
    $last = $seen[$id]
    if (-not $firstRun -and $null -ne $last) {
        if ($last -eq $current) { return }          # nothing new since the last check
        if (-not $updated) {
            $adopted[$id] = $true
            $seen[$id] = $current
            $script:released++
            Write-Log ('changed without an update, left to the user (no longer guarded): {0} = {1}' -f $label, $current)
            return
        }
    }
    if ($reportOnly) {
        $seen[$id] = $current
        $script:reported++
        Write-Log ('not as wanted (report only): {0} = {1}, wanted {2}' -f $label, $current, $wanted)
        return
    }
    try {
        & $repair
        $seen[$id] = $wanted
        $script:repaired++
        Write-Log ('repaired: {0} = {1} -> {2}' -f $label, $current, $wanted)
    } catch {
        $seen[$id] = $current
        Write-Log ('could not repair {0}: {1}' -f $label, $_.Exception.Message)
    }
}

# ---- services ---------------------------------------------------------------------------------

foreach ($s in @($plan.services)) {
    if (-not $s) { continue }
    $name = [string] $s.name
    $path = 'SYSTEM\CurrentControlSet\Services\' + $name
    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($path)
    if (-not $key) { continue }                       # not on this Windows (or removed with its component)
    $start = $key.GetValue('Start', $null)
    $delayed = [int] $key.GetValue('DelayedAutostart', 0)
    $key.Close()
    if ($null -eq $start) { continue }
    $wantStart = [int] $s.start
    $wantDelayed = [int] [bool] $s.delayed
    $wanted = [string] $wantStart
    $current = [string] $start
    if ($wantStart -eq 2) {
        $wanted += '/' + $wantDelayed
        $current += '/' + $delayed
    }
    $fix = {
        $w = [Microsoft.Win32.Registry]::LocalMachine.CreateSubKey($path)
        $w.SetValue('Start', $wantStart, [Microsoft.Win32.RegistryValueKind]::DWord)
        if ($wantStart -eq 2) { $w.SetValue('DelayedAutostart', $wantDelayed, [Microsoft.Win32.RegistryValueKind]::DWord) }
        $w.Close()
    }.GetNewClosure()
    Resolve-Item ('service:' + $name) $wanted $current ('service ' + $name) $fix
}

# ---- machine-wide registry values -------------------------------------------------------------

function Get-ValueText($key, [string] $name) {
    if (-not $key -or -not ($key.GetValueNames() -contains $name)) { return 'absent' }
    $kind = $key.GetValueKind($name)
    $raw = $key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    switch ($kind) {
        'DWord' { return 'dword:' + [BitConverter]::ToUInt32([BitConverter]::GetBytes([int] $raw), 0) }
        'QWord' { return 'qword:' + [BitConverter]::ToUInt64([BitConverter]::GetBytes([long] $raw), 0) }
        'String' { return 'sz:' + [string] $raw }
        'ExpandString' { return 'expandsz:' + [string] $raw }
        'MultiString' { return 'multisz:' + (@($raw) -join "`n") }
        'Binary' { return 'binary:' + ((@($raw) | ForEach-Object { $_.ToString('x2') }) -join '') }
        default { return 'other' }
    }
}

foreach ($v in @($plan.values)) {
    if (-not $v) { continue }
    $sub = ([string] $v.key) -replace '^HKLM\\', ''
    $name = [string] $v.name
    $type = [string] $v.type
    $data = [string] $v.data
    $key = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($sub)
    $current = Get-ValueText $key $name
    if ($key) { $key.Close() }
    $wanted = if ($type -eq 'delete') { 'absent' } else { $type + ':' + $data }
    $fix = {
        if ($type -eq 'delete') {
            $w = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($sub, $true)
            if ($w) {
                $w.DeleteValue($name, $false)
                $w.Close()
            }
            return
        }
        $w = [Microsoft.Win32.Registry]::LocalMachine.CreateSubKey($sub)
        switch ($type) {
            'dword' { $w.SetValue($name, [BitConverter]::ToInt32([BitConverter]::GetBytes([uint32] $data), 0), [Microsoft.Win32.RegistryValueKind]::DWord) }
            'qword' { $w.SetValue($name, [BitConverter]::ToInt64([BitConverter]::GetBytes([uint64] $data), 0), [Microsoft.Win32.RegistryValueKind]::QWord) }
            'sz' { $w.SetValue($name, $data, [Microsoft.Win32.RegistryValueKind]::String) }
            'expandsz' { $w.SetValue($name, $data, [Microsoft.Win32.RegistryValueKind]::ExpandString) }
            'multisz' { $w.SetValue($name, [string[]] ($data -split "`n"), [Microsoft.Win32.RegistryValueKind]::MultiString) }
            'binary' {
                $bytes = New-Object byte[] ($data.Length / 2)
                for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = [Convert]::ToByte($data.Substring(2 * $i, 2), 16) }
                $w.SetValue($name, $bytes, [Microsoft.Win32.RegistryValueKind]::Binary)
            }
        }
        $w.Close()
    }.GetNewClosure()
    $label = 'HKLM\{0}\{1}' -f $sub, $(if ($name) { $name } else { '(default)' })
    Resolve-Item ('value:' + $sub + '::' + $name) $wanted $current $label $fix
}

# ---- scheduled tasks switched off -------------------------------------------------------------

foreach ($t in @($plan.tasks)) {
    if (-not $t) { continue }
    $full = [string] $t
    if (-not $full.StartsWith('\')) { $full = '\' + $full }
    $cut = $full.LastIndexOf('\')
    $tp = $full.Substring(0, $cut + 1)
    $tn = $full.Substring($cut + 1)
    $task = Get-ScheduledTask -TaskPath $tp -TaskName $tn -ErrorAction SilentlyContinue
    if (-not $task) { continue }
    $current = if ([string] $task.State -eq 'Disabled') { 'disabled' } else { 'enabled' }
    $fix = { Disable-ScheduledTask -TaskPath $tp -TaskName $tn -ErrorAction Stop | Out-Null }.GetNewClosure()
    Resolve-Item ('task:' + $full) 'disabled' $current ('task ' + $full) $fix
}

# ---- removed apps -----------------------------------------------------------------------------

$apps = @($plan.apps | Where-Object { $_ })
if ($apps.Count -gt 0) {
    $provisioned = @(Get-AppxProvisionedPackage -Online -ErrorAction SilentlyContinue)
    foreach ($a in $apps) {
        $name = [string] $a.name
        $family = [string] $a.family
        $prov = @($provisioned | Where-Object { $_.DisplayName -eq $name })
        $inst = @(Get-AppxPackage -AllUsers -Name $name -ErrorAction SilentlyContinue)
        # A closure runs in a module of its own: no script functions inside.
        $fix = {
            foreach ($p in $prov) {
                Remove-AppxProvisionedPackage -Online -PackageName $p.PackageName -ErrorAction Stop | Out-Null
                $parts = ([string] $p.PackageName).Split('_')
                if (-not $family -and $parts.Length -ge 2) { $family = $parts[0] + '_' + $parts[$parts.Length - 1] }
            }
            foreach ($p in @(Get-AppxPackage -AllUsers -Name $name -ErrorAction SilentlyContinue)) {
                if (-not $family) { $family = $p.PackageFamilyName }
                Remove-AppxPackage -Package $p.PackageFullName -AllUsers -ErrorAction Stop
            }
            if ($family) {
                $d = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Appx\AppxAllUserStore\Deprovisioned\' + $family
                [Microsoft.Win32.Registry]::LocalMachine.CreateSubKey($d).Close()
            }
        }.GetNewClosure()
        $label = 'app ' + $name
        if ($prov.Count -gt 0) {
            # Provisioned again: only servicing does that, never the user.
            $seen['app:' + $name] = 'provisioned'
            $adopted.Remove('app:' + $name)
            if ($reportOnly) {
                $script:reported++
                Write-Log ('provisioned again (report only): ' + $name)
                continue
            }
            try {
                & $fix
                $seen['app:' + $name] = 'absent'
                $script:repaired++
                Write-Log ('repaired: {0} was provisioned again -> removed' -f $label)
            } catch {
                Write-Log ('could not remove {0}: {1}' -f $label, $_.Exception.Message)
            }
            continue
        }
        $current = if ($inst.Count -gt 0) { 'present' } else { 'absent' }
        Resolve-Item ('app:' + $name) 'absent' $current $label $fix
    }
}

# ---- removed components: reported only --------------------------------------------------------

foreach ($c in @($plan.components)) {
    if (-not $c) { continue }
    $back = @(@($c.paths) | Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $env:SystemDrive ([string] $_))) })
    $id = 'files:' + [string] $c.title
    $current = if ($back.Count -gt 0) { 'back' } else { 'gone' }
    if ($current -eq 'back' -and $seen[$id] -ne 'back') {
        $script:reported++
        Write-Log ('came back (only reported, not deleted): {0} - {1}' -f $c.title, $back[0])
    }
    $seen[$id] = $current
}

# ---- the state for the next check -------------------------------------------------------------

try {
    $out = [ordered]@{ lastRun = $now.ToString('o'); seen = $seen; adopted = @($adopted.Keys) }
    [System.IO.File]::WriteAllText($stateFile, ($out | ConvertTo-Json -Depth 4), $utf8)
} catch {
    Write-Log ('state.json not written: ' + $_.Exception.Message)
}
$note = if ($firstRun) { ' (first check)' } elseif ($updated) { ' (an update was installed since the last check)' } else { '' }
Write-Log ('check done{0}: {1} repaired, {2} reported, {3} left to the user' -f $note, $script:repaired, $script:reported, $script:released)
exit 0
