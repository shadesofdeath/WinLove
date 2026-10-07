<#
  Lab tool (elevated): why does Setup fail on a given media? Windows Setup in WinPE keeps its logs in
  X:\ (a RAM disk) and they are gone when the VM stops. This wraps Setup in a copy of the media's
  boot.wim: HKLM\SYSTEM\Setup\CmdLine runs X:\wldbg.cmd, which gives the lab's diagnostics disk
  (lab_vm.ps1 -LogDisk, the second disk) the letter L:, starts Setup, copies Setup's logs there
  every 10 s for -Minutes, then shuts down. lab_vm collects the disk into build\lab\out\vm-setuplogs-<Tag>\diag.
  Usage: lab_setup_logs.ps1 -Tag x -SetupFolder <media> -InstallWim <wim> [-IsoArgs ...]
  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe",
    [Parameter(Mandatory = $true)] [string] $Tag,
    [Parameter(Mandatory = $true)] [string] $SetupFolder,
    [Parameter(Mandatory = $true)] [string] $InstallWim,
    [string[]] $IsoArgs = @(),
    [int] $Minutes = 5,
    [int] $VncPort = 5920
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$Cli = [System.IO.Path]::GetFullPath($Cli)
$work = Join-Path $Lab "work\setuplogs-$Tag"
$mount = Join-Path $Lab "mount\setuplogs-$Tag"
foreach ($dir in $work, $mount) { if (Test-Path $dir) { Remove-Item $dir -Recurse -Force } }
New-Item -ItemType Directory -Force $work, $mount | Out-Null

# 1. A boot.wim whose Setup runs inside the log copier.
$boot = Join-Path $work 'boot.wim'
Copy-Item (Join-Path $SetupFolder 'sources\boot.wim') $boot
& $Cli mount $boot 2 $mount | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'mount of boot.wim index 2 failed' }
$loops = $Minutes * 6
$cmd = @(
    '@echo off',
    'wpeinit',
    # The data partition of the 512 MB disk: after the MSR on a GPT disk; which disk number the SATA
    # disk gets next to the SCSI one is not fixed. Every try may fail (noerr), one gives it L:.
    # diskpart stops a script at its first error (and takes noerr for few commands): one run per try.
    'for %%d in (0 1) do for %%p in (2 1) do (',
    '  (echo select disk %%d& echo select partition %%p& echo assign letter=L) > X:\wldp.txt',
    '  diskpart /s X:\wldp.txt >> X:\wldp.log 2>&1',
    ')',
    '(echo list disk& echo list volume) > X:\wldp.txt',
    'diskpart /s X:\wldp.txt >> X:\wldp.log 2>&1',
    'type X:\wldp.log',
    'dir L:\',
    'ping -n 21 127.0.0.1 > nul',
    'start "" X:\setup.exe',
    "for /l %%i in (1,1,$loops) do (",
    '  xcopy "X:\$WINDOWS.~BT\Sources\Panther" L:\logs\bt\ /s /y /h /i /c > nul 2>&1',
    # Setup copies the media to <disk>:\$WINDOWS.~BT and goes on from there: its next stage logs there.
    '  for %%v in (C D E F G H I J K) do xcopy "%%v:\$WINDOWS.~BT\Sources\Panther" L:\logs\bt-%%v\ /s /y /h /i /c > nul 2>&1',
    '  xcopy "X:\Windows\Panther" L:\logs\pe\ /s /y /h /i /c > nul 2>&1',
    '  xcopy "X:\Windows\Logs" L:\logs\winlogs\ /s /y /h /i /c > nul 2>&1',
    '  copy /y X:\wldp.log L:\logs\ > nul 2>&1',
    '  ping -n 11 127.0.0.1 > nul',
    ')',
    'wpeutil shutdown'
) -join "`r`n"
[System.IO.File]::WriteAllText((Join-Path $mount 'wldbg.cmd'), $cmd + "`r`n", (New-Object System.Text.ASCIIEncoding))
& reg.exe load HKLM\WLDBG (Join-Path $mount 'Windows\System32\config\SYSTEM') | Out-Null
& reg.exe add HKLM\WLDBG\Setup /v CmdLine /t REG_SZ /d 'cmd /c X:\wldbg.cmd' /f | Out-Null
[gc]::Collect()
& reg.exe unload HKLM\WLDBG | Out-Null
& $Cli unmount $mount --commit | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'commit of the debug boot.wim failed' }

# 2. The VM; lab_vm collects the diagnostics disk.
& (Join-Path $PSScriptRoot 'lab_vm.ps1') -Tag "setuplogs-$Tag" -SetupFolder $SetupFolder -InstallWim $InstallWim -ImageIndex 1 `
    -BootWim $boot -IsoArgs $IsoArgs -ShutdownAfter 0 -TimeoutMinutes ($Minutes + 12) -VncPort $VncPort -LogDisk
$logs = Join-Path $Lab "out\vm-setuplogs-$Tag\diag"
Get-ChildItem $logs -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 40 | ForEach-Object { $_.FullName.Substring($logs.Length) + '  ' + $_.Length }
$err = Get-ChildItem $logs -Recurse -Filter 'setuperr.log' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($err) { "`n--- " + $err.FullName; Get-Content $err.FullName -Tail 40 }
