<#
  Guest side of tools\lab_vm.ps1 -Diag. Runs inside the lab VM as SYSTEM (a task registered in the
  specialize pass), from before OOBE until DiagMinutes after the first sign-in, then shuts the guest down.
  - timeline.txt: every 30 s, what appeared / disappeared: desktop items (every profile, Public,
    OneDrive desktops), Start menu shortcuts, apps (all users), Run entries, OneDrive files,
    update-scheduler (UScheduler / UScheduler_Oobe) entries, interesting processes.
  - at the end: event logs (Security with process creation + command lines, AppX deployment, Task
    Scheduler, Shell-Core, Store), registry exports, task list, app lists, USO / Panther logs, the
    desktop shortcuts and their targets.
  Everything goes to the partition labelled WLDIAG (a second virtual disk the host reads afterwards),
  mounted on a folder: no drive letter.
  Keep this file plain ASCII.
#>
param([int] $DiagMinutes = 30, [int] $CapMinutes = 100)
$ErrorActionPreference = 'Continue'
$base = 'C:\ProgramData\WinLoveDiag'
$out = Join-Path $base 'out'
New-Item -ItemType Directory -Force $base, $out | Out-Null
$timeline = Join-Path $base 'timeline.txt'
$startFile = Join-Path $base 'started.txt'
$logonFile = Join-Path $base 'logon.txt'
if (-not (Test-Path $startFile)) { [string](Get-Date).Ticks | Set-Content $startFile }
$started = [datetime][long](Get-Content $startFile)

function Line([string] $text) { Add-Content -Path $timeline -Value ((Get-Date).ToString('HH:mm:ss') + '  ' + $text) -Encoding UTF8 }

function MountDiag {
    if (Test-Path (Join-Path $out 'WLDIAG.txt')) { return $true }
    $part = Get-Partition -ErrorAction SilentlyContinue | Where-Object {
        $v = $_ | Get-Volume -ErrorAction SilentlyContinue; $v -and $v.FileSystemLabel -eq 'WLDIAG' } | Select-Object -First 1
    if (-not $part) { return $false }
    try {
        Add-PartitionAccessPath -DiskNumber $part.DiskNumber -PartitionNumber $part.PartitionNumber -AccessPath ($out + '\') -ErrorAction Stop
        'diag disk' | Set-Content (Join-Path $out 'WLDIAG.txt')
        Line "diag disk mounted (disk $($part.DiskNumber))"
        return $true
    } catch { Line ("diag disk mount failed: " + $_.Exception.Message); return $false }
}

$prev = @{}
function Track([string] $name, $now) {
    $now = @($now | Where-Object { $_ } | Sort-Object -Unique)
    $old = $prev[$name]
    if ($null -eq $old) { $old = @() }
    foreach ($x in $now) { if ($old -notcontains $x) { Line "+ $name  $x" } }
    foreach ($x in $old) { if ($now -notcontains $x) { Line "- $name  $x" } }
    $prev[$name] = $now
}

function Desktops {
    $dirs = @('C:\Users\Public\Desktop') + @(Get-ChildItem 'C:\Users' -Directory -Force -ErrorAction SilentlyContinue | ForEach-Object {
        Join-Path $_.FullName 'Desktop'
        Get-ChildItem $_.FullName -Directory -Filter 'OneDrive*' -Force -ErrorAction SilentlyContinue | ForEach-Object { Join-Path $_.FullName 'Desktop' } })
    foreach ($d in $dirs) {
        Get-ChildItem -LiteralPath $d -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'desktop.ini' } |
            ForEach-Object { $_.FullName + '  (created ' + $_.CreationTime.ToString('HH:mm:ss') + ')' }
    }
}

function StartMenu {
    foreach ($d in @('C:\ProgramData\Microsoft\Windows\Start Menu\Programs') + @(Get-ChildItem 'C:\Users' -Directory -Force -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName 'AppData\Roaming\Microsoft\Windows\Start Menu\Programs' })) {
        Get-ChildItem -LiteralPath $d -Recurse -Filter '*.lnk' -Force -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
    }
}

function RunEntries {
    $keys = @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Run', 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\RunOnce',
              'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Run')
    foreach ($u in @(Get-ChildItem 'Registry::HKEY_USERS' -ErrorAction SilentlyContinue)) {
        $keys += "Registry::$($u.Name)\Software\Microsoft\Windows\CurrentVersion\Run"
        $keys += "Registry::$($u.Name)\Software\Microsoft\Windows\CurrentVersion\RunOnce"
    }
    foreach ($k in $keys) {
        $item = Get-Item -LiteralPath $k -ErrorAction SilentlyContinue
        if ($item) { foreach ($n in $item.GetValueNames()) { ($k -replace '^Registry::', '') + ' : ' + $n + ' = ' + $item.GetValue($n) } }
    }
}

function Scheduler {
    foreach ($k in 'HKLM:\SOFTWARE\Microsoft\WindowsUpdate\Orchestrator\UScheduler_Oobe', 'HKLM:\SOFTWARE\Microsoft\WindowsUpdate\Orchestrator\UScheduler') {
        Get-ChildItem -LiteralPath $k -ErrorAction SilentlyContinue | ForEach-Object {
            $p = Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction SilentlyContinue
            ($k -replace '^HKLM:\\SOFTWARE\\Microsoft\\WindowsUpdate\\Orchestrator\\', '') + '\' + $_.PSChildName +
                ' workCompleted=' + $p.workCompleted + ' lastRun=' + $p.lastRunTime
        }
    }
    Get-ChildItem 'C:\ProgramData\USOPrivate\ExpeditedAppRegistrations' -Force -ErrorAction SilentlyContinue | ForEach-Object { 'Expedited\' + $_.Name }
}

function OneDriveFiles {
    $paths = @('C:\Windows\System32\OneDriveSetup.exe', 'C:\Windows\SysWOW64\OneDriveSetup.exe', 'C:\Program Files\Microsoft OneDrive\OneDrive.exe',
               'C:\Program Files (x86)\Microsoft OneDrive\OneDrive.exe')
    $paths += @(Get-ChildItem 'C:\Users' -Directory -Force -ErrorAction SilentlyContinue | ForEach-Object {
        Join-Path $_.FullName 'AppData\Local\Microsoft\OneDrive\OneDrive.exe'; Join-Path $_.FullName 'OneDrive' })
    foreach ($p in $paths) { if (Test-Path -LiteralPath $p) { $p } }
}

$watch = 'onedrive|outlook|olk|office|m365|teams|setup|install|uscheduler|usoclient|moussocoreworker|appinstaller|winget|bcilauncher|devhome'
$loop = 0
$finishing = $false
Line ("diag start; boot " + (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('s'))
while ($true) {
    $loop++
    $mounted = MountDiag
    try { Track 'desktop' (Desktops) } catch { }
    try { Track 'start' (StartMenu) } catch { }
    try { Track 'run' (RunEntries) } catch { }
    try { Track 'sched' (Scheduler) } catch { }
    try { Track 'file' (OneDriveFiles) } catch { }
    try { Track 'appx' (Get-AppxPackage -AllUsers -ErrorAction Stop | ForEach-Object { $_.PackageFullName }) } catch { }
    try {
        Track 'proc' (Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object { ($_.Name + ' ' + $_.CommandLine) -match $watch } |
            ForEach-Object { $_.Name + ' | ' + $_.CommandLine })
    } catch { }
    # The lab account's Explorer (OOBE runs one as defaultuser0 too). Not 'Diff': that is an alias of Compare-Object.
    $explorer = @(Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" -ErrorAction SilentlyContinue |
        Where-Object { (Invoke-CimMethod -InputObject $_ -MethodName GetOwner -ErrorAction SilentlyContinue).User -eq 'lab' })
    if ($explorer.Count -gt 0 -and -not (Test-Path $logonFile)) {
        [string](Get-Date).Ticks | Set-Content $logonFile
        Line 'first sign-in: explorer.exe is running'
        $net = Test-Connection -ComputerName 'www.microsoft.com' -Count 2 -Quiet -ErrorAction SilentlyContinue
        Line ("internet: " + $(if ($net) { 'yes' } else { 'NO (the measurement is not representative)' }))
    }
    if ($mounted) { Copy-Item $timeline (Join-Path $out 'timeline.txt') -Force -ErrorAction SilentlyContinue }
    $now = Get-Date
    $logon = if (Test-Path $logonFile) { [datetime][long](Get-Content $logonFile) } else { $null }
    if (($logon -and ($now - $logon).TotalMinutes -ge $DiagMinutes) -or ($now - $started).TotalMinutes -ge $CapMinutes) { break }
    Start-Sleep -Seconds 30
}

Line 'collecting'
if (-not (MountDiag)) { Line 'no diag disk: results stay in C:\ProgramData\WinLoveDiag\out' }
$logs = @{ 'Security' = 'Security'; 'System' = 'System'; 'Application' = 'Application'
           'Microsoft-Windows-AppXDeploymentServer/Operational' = 'AppxDeploymentServer'
           'Microsoft-Windows-AppXDeployment/Operational' = 'AppxDeployment'
           'Microsoft-Windows-TaskScheduler/Operational' = 'TaskScheduler'
           'Microsoft-Windows-Shell-Core/Operational' = 'ShellCore'
           'Microsoft-Windows-Store/Operational' = 'Store'
           'Microsoft-Windows-AppReadiness/Admin' = 'AppReadinessAdmin'
           'Microsoft-Windows-AppReadiness/Operational' = 'AppReadiness' }
foreach ($k in $logs.Keys) { & wevtutil.exe epl $k (Join-Path $out ($logs[$k] + '.evtx')) 2>$null }
$sid = (Get-CimInstance Win32_UserAccount -Filter "Name='lab'" -ErrorAction SilentlyContinue).SID
$exports = @{ 'orchestrator' = 'HKLM\SOFTWARE\Microsoft\WindowsUpdate\Orchestrator'
              'appxallusers' = 'HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Appx\AppxAllUserStore'
              'policies' = 'HKLM\SOFTWARE\Policies'
              'hklm-run' = 'HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Run'
              'hklm-onedrive' = 'HKLM\SOFTWARE\Microsoft\OneDrive'
              'hklm-uninstall' = 'HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall' }
if ($sid) {
    $exports['user-run'] = "HKU\$sid\Software\Microsoft\Windows\CurrentVersion\Run"
    $exports['user-runonce'] = "HKU\$sid\Software\Microsoft\Windows\CurrentVersion\RunOnce"
    $exports['user-cdm'] = "HKU\$sid\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager"
    $exports['user-onedrive'] = "HKU\$sid\Software\Microsoft\OneDrive"
    $exports['user-uninstall'] = "HKU\$sid\Software\Microsoft\Windows\CurrentVersion\Uninstall"
    $exports['user-policies'] = "HKU\$sid\Software\Policies"
}
foreach ($k in $exports.Keys) { & reg.exe export $exports[$k] (Join-Path $out ($k + '.reg')) /y 2>$null | Out-Null }
& schtasks.exe /query /fo csv /v > (Join-Path $out 'tasks.csv') 2>$null
try { Get-AppxPackage -AllUsers | Select-Object Name, PackageFullName, InstallLocation, SignatureKind, NonRemovable,
        @{ n = 'Users'; e = { ($_.PackageUserInformation | ForEach-Object { $_.UserSecurityId.Username + ':' + $_.InstallState }) -join ';' } } |
        Export-Csv (Join-Path $out 'appx.csv') -NoTypeInformation -Encoding UTF8 } catch { Line ('appx list: ' + $_.Exception.Message) }
try { Get-AppxProvisionedPackage -Online | Select-Object DisplayName, PackageName, Version | Export-Csv (Join-Path $out 'provisioned.csv') -NoTypeInformation -Encoding UTF8 } catch { }
$shell = New-Object -ComObject WScript.Shell
$links = foreach ($d in (Desktops)) {
    $p = ($d -split '  \(created ')[0]
    $t = ''
    if ($p -like '*.lnk') { try { $l = $shell.CreateShortcut($p); $t = $l.TargetPath + ' ' + $l.Arguments } catch { } }
    $i = Get-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue
    $p + ' | created ' + $i.CreationTime.ToString('s') + ' | target ' + $t
}
$links | Set-Content (Join-Path $out 'desktop.txt') -Encoding UTF8
StartMenu | Set-Content (Join-Path $out 'startmenu.txt') -Encoding UTF8
foreach ($pair in @(@('C:\ProgramData\USOPrivate', 'USOPrivate'), @('C:\ProgramData\USOShared\Logs', 'USOSharedLogs'), @('C:\Windows\Panther', 'Panther'),
                    @('C:\Users\Public\Desktop', 'PublicDesktop'), @('C:\Users\lab\Desktop', 'LabDesktop'),
                    @('C:\Users\lab\AppData\Local\Microsoft\OneDrive\setup\logs', 'OneDriveSetupLogs'))) {
    & robocopy.exe $pair[0] (Join-Path $out $pair[1]) /E /B /R:0 /W:0 /NFL /NDL /NJH /NJS /NP /XF *.etl.tmp | Out-Null
}
Line 'done; shutting down'
Copy-Item $timeline (Join-Path $out 'timeline.txt') -Force
'done' | Set-Content (Join-Path $out 'done.txt')
& shutdown.exe /s /t 5 /f
