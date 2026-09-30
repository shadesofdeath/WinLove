<#
.SYNOPSIS
  Engine check for the USB writer (core/usb/UsbMedia: writeUsb) without a USB stick: a VHDX in
  build\lab stands in for the stick. It is created and attached, wlcli usb-write writes the setup
  folder to it (diskpart clean + FAT32, bootsect, copy, install.wim split into .swm), then the
  script checks the result: partition style and active flag, FAT32, the boot sector (BOOTMGR),
  the boot files, the .swm parts (DISM reads the editions from them), no install.wim > 4 GB.
  The VHDX is detached and deleted at the end (-Keep leaves it, e.g. to boot it in a VM).
  With -Gpt the UEFI-only layout is written instead (GPT, no active partition).

  Needs an elevated PowerShell (about 5 minutes, 9 GB free) and the setup folder of the test ISO:
    build\x64-debug\bin\wlcli.exe extract-all <test.iso> build\lab\setup      (no admin)
    powershell -ExecutionPolicy Bypass -File tools\lab_usb.ps1
    powershell -ExecutionPolicy Bypass -File tools\lab_usb.ps1 -Gpt
  The whole output is also written to build\lab\out\usb-test.log (UTF-8).
  No real disk is touched: usb-write only accepts the VHDX's disk number (--allow-virtual) and
  checks the disk again right before it erases it.

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [switch] $Gpt,
    [switch] $Keep,
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$setup = Join-Path $Lab 'setup'
$vhd = Join-Path $Lab 'usb\stick.vhdx'
$log = Join-Path $Lab 'out\usb-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path (Join-Path $setup 'sources\install.wim'))) {
    throw "Setup folder missing: run  wlcli extract-all <test.iso> $setup  first"
}
foreach ($dir in 'usb', 'out') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }

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
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*usb\s+[\d]+%\s*$' } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*usb\s+\d+%)+', '').TrimEnd()) }
    Say ("  (exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $output
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}
function DiskPart([string[]] $lines) {
    $script = Join-Path $Lab 'usb\diskpart.txt'
    Set-Content -Path $script -Value $lines -Encoding ASCII
    $out = Native { diskpart.exe /s $script }
    Remove-Item $script -Force
    return $out
}

Say ("=== lab_usb " + (Get-Date -Format s) + ($(if ($Gpt) { ' (GPT, UEFI only)' } else { ' (MBR, BIOS + UEFI)' })))
if (Test-Path $vhd) {
    DiskPart @("select vdisk file=`"$vhd`"", 'detach vdisk noerr') | Out-Null
    Remove-Item $vhd -Force
}
$out = DiskPart @("create vdisk file=`"$vhd`" maximum=16000 type=expandable", "select vdisk file=`"$vhd`"", 'attach vdisk')
Check 'VHDX created and attached (16 GB, expandable)' ($LASTEXITCODE -eq 0)
try {
    Start-Sleep -Seconds 2
    $disk = Get-DiskImage -ImagePath $vhd | Get-Disk
    Say "VHDX is disk $($disk.Number) ($($disk.BusType))"

    $list = (Native { & $Cli usb-list --allow-virtual --json } | Out-String) | ConvertFrom-Json
    Check 'usb-list --allow-virtual shows the VHDX' (@($list | Where-Object { $_.disk -eq $disk.Number }).Count -eq 1)
    $plain = (Native { & $Cli usb-list --json } | Out-String) | ConvertFrom-Json
    Check 'usb-list without --allow-virtual does not' (@($plain | Where-Object { $_.disk -eq $disk.Number }).Count -eq 0)
    Run @('usb-write', "$($disk.Number)", $setup, '--allow-virtual') | Out-Null
    Check 'usb-write without --yes refuses' ($script:lastExit -ne 0)

    $arguments = @('usb-write', "$($disk.Number)", $setup, '--yes', '--allow-virtual', '--label=WINLOVE', '--verbose')
    if ($Gpt) { $arguments += '--gpt' }
    Run $arguments | Out-Null
    Check 'usb-write finished' ($script:lastExit -eq 0)

    $disk = Get-Disk -Number $disk.Number
    $part = @(Get-Partition -DiskNumber $disk.Number | Where-Object { $_.DriveLetter })
    Check 'one partition with a drive letter' ($part.Count -eq 1)
    $letter = $part[0].DriveLetter
    $root = "${letter}:\"
    if ($Gpt) {
        Check 'partition style GPT' ($disk.PartitionStyle -eq 'GPT')
    } else {
        Check 'partition style MBR' ($disk.PartitionStyle -eq 'MBR')
        Check 'partition is active (BIOS boot)' ($part[0].IsActive)
    }
    $volume = Get-Volume -DriveLetter $letter
    Check "file system FAT32 (label $($volume.FileSystemLabel))" ($volume.FileSystem -eq 'FAT32')

    foreach ($file in 'bootmgr', 'bootmgr.efi', 'efi\boot\bootx64.efi', 'boot\bcd', 'sources\boot.wim', 'setup.exe') {
        Check "file $file" (Test-Path (Join-Path $root $file))
    }
    Check 'no install.wim on the stick' (-not (Test-Path (Join-Path $root 'sources\install.wim')))
    $parts = @(Get-ChildItem (Join-Path $root 'sources') -Filter 'install*.swm')
    Check "install.wim split into .swm parts ($($parts.Count))" ($parts.Count -ge 2)
    Check 'every part under 4 GB' (@($parts | Where-Object { $_.Length -ge 4GB }).Count -eq 0)
    $big = @(Get-ChildItem $root -Recurse -File | Where-Object { $_.Length -ge 4GB })
    Check 'no file of 4 GB or more' ($big.Count -eq 0)
    $sourceBytes = (Get-ChildItem $setup -Recurse -File | Measure-Object Length -Sum).Sum
    $stickBytes = (Get-ChildItem $root -Recurse -File | Measure-Object Length -Sum).Sum
    Say ("source {0:N0} bytes, stick {1:N0} bytes" -f $sourceBytes, $stickBytes)
    Check 'the stick holds about as much as the folder (split adds headers)' ([math]::Abs($stickBytes - $sourceBytes) -lt 64MB)

    $info = Native { dism.exe /English /Get-WimInfo "/WimFile:$(Join-Path $root 'sources\install.swm')" }
    $editions = @($info | Where-Object { $_ -match '^Index\s*:' }).Count
    Say "DISM reads $editions edition(s) from install.swm"
    Check 'DISM reads the editions of the split image' ($editions -ge 1)

    # Volume boot sector: the NT6 code looks for BOOTMGR.
    $stream = [System.IO.File]::Open("\\.\${letter}:", 'Open', 'Read', 'ReadWrite')
    $sector = New-Object byte[] 512
    [void]$stream.Read($sector, 0, 512)
    $stream.Close()
    $text = [System.Text.Encoding]::ASCII.GetString($sector)
    Check 'boot sector ends with 55 AA' ($sector[510] -eq 0x55 -and $sector[511] -eq 0xAA)
    if (-not $Gpt) { Check 'boot sector loads BOOTMGR' ($text.Contains('BOOTMGR')) }
} finally {
    if ($Keep) {
        Say "VHDX kept: $vhd (detach: diskpart  select vdisk file=... / detach vdisk)"
    } else {
        DiskPart @("select vdisk file=`"$vhd`"", 'detach vdisk') | Out-Null
        Remove-Item $vhd -Force -ErrorAction SilentlyContinue
        Check 'VHDX detached and deleted' (-not (Test-Path $vhd))
    }
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
