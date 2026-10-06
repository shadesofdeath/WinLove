<#
.SYNOPSIS
  Proof for D-074 (boot into the previous Setup).
    1. Engine: sources\boot.wim of the test ISO is copied into build\lab, patched with
       wlcli boot-patch --legacy-setup, committed, mounted once more: HKLM\SYSTEM\Setup\CmdLine must
       read the previous-Setup command, X:\sources\setup.exe must be there, every stream sound.
  With -Vm (VMware, ~60 min) the failure it answers is reproduced and the fix installed:
    2. edition 4 of build\lab\setup is exported, mounted, Windows\System32\Recovery\Winre.wim is
       deleted (what the "winre" component does) and committed;
    3. "new"    - the ISO keeps the original boot.wim (the new Setup). Expected: Setup stops with an
                  error; the guest never reaches the desktop (-FailMinutes).
       "legacy" - the same ISO with the patched boot.wim. Expected: unattended install to the
                  desktop; the guest shuts itself down after the first sign-in.
       Both VMs: EFI, SATA 64 GB, 4 GB, no network, VNC on 127.0.0.1:5917; the VM's own screen is
       captured every 30 s (tools\vnc_shot.py) into build\lab\out\vm-legacy-<run>. The host's screen
       is never captured.
  Nothing outside build\lab is touched; the ISO is only read; VMs, ISOs and work files are deleted.

    powershell -ExecutionPolicy Bypass -File tools\lab_legacy_setup.ps1        (elevated; ~3 min)
    powershell -ExecutionPolicy Bypass -File tools\lab_legacy_setup.ps1 -Vm    (elevated; ~60 min)

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $Iso = 'C:\Users\shades\Downloads\Win11_25H2_Turkish_x64_v2.iso',
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe",
    [switch] $Vm,
    [int] $Edition = 4,
    [int] $FailMinutes = 15,
    [int] $TimeoutMinutes = 80,
    [switch] $KeepVm
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$setup = Join-Path $Lab 'setup'
$work = Join-Path $Lab 'work\legacy'
$bootWim = Join-Path $work 'boot.wim'
$bootMount = Join-Path $Lab 'mount\legacy-boot'
$mount = Join-Path $Lab 'mount\legacy'
$log = Join-Path $Lab 'out\legacy-setup-test.log'
$expected = 'cmd /c start /min wpeinit && \sources\setup'
$vmware = 'C:\Program Files (x86)\VMware\VMware Workstation'
$vmrun = Join-Path $vmware 'vmrun.exe'
$vdisk = Join-Path $vmware 'vmware-vdiskmanager.exe'
$vncPort = 5917

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
foreach ($need in $Cli, $Iso) { if (-not (Test-Path $need)) { throw "missing: $need" } }
if ($Vm) {
    foreach ($need in $vmrun, $vdisk, (Join-Path $setup 'sources\install.wim'), (Join-Path $setup 'efi\microsoft\boot\efisys_noprompt.bin')) {
        if (-not (Test-Path $need)) { throw "missing: $need" }
    }
}
foreach ($dir in 'out', 'mount', 'vm') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
foreach ($dir in $bootMount, $mount) {
    if (Test-Path (Join-Path $dir 'Windows')) { & $Cli unmount $dir --discard | Out-Null }
}
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work, $bootMount, $mount | Out-Null
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
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export|iso|boot|verify|extract|unmount)\s+)?[\d.]+(%| GB)\s*$' } |
        Select-Object -Last 8 | ForEach-Object { Say ("  " + ($_ -replace '^(\s*((verify|boot|extract|iso)\s+)?[\d.]+%)+', '').TrimEnd()) }
    Say ("  (exit $script:lastExit)")
    return $output
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }
function VmRunning([string] $vmx) { (@(Native { & $vmrun -T ws list }) -join "`n") -match [regex]::Escape($vmx) }
# HKLM\SYSTEM\Setup\CmdLine of a mounted image.
function CmdLine([string] $dir) {
    $name = 'WinLoveLegacyCheck'
    $hive = Join-Path $dir 'Windows\System32\config\SYSTEM'
    Native { reg.exe load "HKLM\$name" $hive } | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "could not load $hive" }
    try { $value = (Get-ItemProperty -Path "Registry::HKEY_LOCAL_MACHINE\$name\Setup" -Name CmdLine).CmdLine }
    finally { [gc]::Collect(); Native { reg.exe unload "HKLM\$name" } | Out-Null }
    return $value
}

$unattend = @'
<?xml version="1.0" encoding="utf-8"?>
<unattend xmlns="urn:schemas-microsoft-com:unattend" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
  <settings pass="windowsPE">
    <component name="Microsoft-Windows-International-Core-WinPE" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <SetupUILanguage><UILanguage>tr-TR</UILanguage></SetupUILanguage>
      <InputLocale>041f:0000041f</InputLocale><SystemLocale>tr-TR</SystemLocale><UILanguage>tr-TR</UILanguage><UserLocale>tr-TR</UserLocale>
    </component>
    <component name="Microsoft-Windows-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <RunSynchronous>
        <RunSynchronousCommand wcm:action="add"><Order>1</Order><Path>reg add HKLM\SYSTEM\Setup\LabConfig /v BypassTPMCheck /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>
        <RunSynchronousCommand wcm:action="add"><Order>2</Order><Path>reg add HKLM\SYSTEM\Setup\LabConfig /v BypassSecureBootCheck /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>
        <RunSynchronousCommand wcm:action="add"><Order>3</Order><Path>reg add HKLM\SYSTEM\Setup\LabConfig /v BypassRAMCheck /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>
      </RunSynchronous>
      <DiskConfiguration>
        <Disk wcm:action="add">
          <DiskID>0</DiskID><WillWipeDisk>true</WillWipeDisk>
          <CreatePartitions>
            <CreatePartition wcm:action="add"><Order>1</Order><Type>EFI</Type><Size>300</Size></CreatePartition>
            <CreatePartition wcm:action="add"><Order>2</Order><Type>MSR</Type><Size>16</Size></CreatePartition>
            <CreatePartition wcm:action="add"><Order>3</Order><Type>Primary</Type><Extend>true</Extend></CreatePartition>
          </CreatePartitions>
          <ModifyPartitions>
            <ModifyPartition wcm:action="add"><Order>1</Order><PartitionID>1</PartitionID><Format>FAT32</Format><Label>System</Label></ModifyPartition>
            <ModifyPartition wcm:action="add"><Order>2</Order><PartitionID>2</PartitionID></ModifyPartition>
            <ModifyPartition wcm:action="add"><Order>3</Order><PartitionID>3</PartitionID><Format>NTFS</Format><Label>Windows</Label><Letter>C</Letter></ModifyPartition>
          </ModifyPartitions>
        </Disk>
      </DiskConfiguration>
      <ImageInstall><OSImage>
        <InstallTo><DiskID>0</DiskID><PartitionID>3</PartitionID></InstallTo>
        <InstallFrom><MetaData wcm:action="add"><Key>/IMAGE/INDEX</Key><Value>1</Value></MetaData></InstallFrom>
      </OSImage></ImageInstall>
      <UserData><AcceptEula>true</AcceptEula><ProductKey><Key>VK7JG-NPHTM-C97JM-9MPGT-3V66T</Key><WillShowUI>Never</WillShowUI></ProductKey></UserData>
    </component>
  </settings>
  <settings pass="specialize">
    <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <ComputerName>WL-LEGACY</ComputerName>
    </component>
    <component name="Microsoft-Windows-Deployment" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <RunSynchronous>
        <RunSynchronousCommand wcm:action="add"><Order>1</Order><Path>reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\OOBE /v BypassNRO /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>
      </RunSynchronous>
    </component>
  </settings>
  <settings pass="oobeSystem">
    <component name="Microsoft-Windows-International-Core" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <InputLocale>041f:0000041f</InputLocale><SystemLocale>tr-TR</SystemLocale><UILanguage>tr-TR</UILanguage><UserLocale>tr-TR</UserLocale>
    </component>
    <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <OOBE>
        <HideEULAPage>true</HideEULAPage><HideOEMRegistrationScreen>true</HideOEMRegistrationScreen>
        <HideOnlineAccountScreens>true</HideOnlineAccountScreens><HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE>
        <ProtectYourPC>3</ProtectYourPC>
      </OOBE>
      <UserAccounts><LocalAccounts>
        <LocalAccount wcm:action="add"><Name>lab</Name><Group>Administrators</Group><Password><Value>lab</Value><PlainText>true</PlainText></Password></LocalAccount>
      </LocalAccounts></UserAccounts>
      <AutoLogon><Enabled>true</Enabled><Username>lab</Username><Password><Value>lab</Value><PlainText>true</PlainText></Password><LogonCount>2</LogonCount></AutoLogon>
      <FirstLogonCommands>
        <SynchronousCommand wcm:action="add"><Order>1</Order><CommandLine>cmd /c timeout /t 60 /nobreak &amp; shutdown /s /t 0</CommandLine></SynchronousCommand>
      </FirstLogonCommands>
    </component>
  </settings>
</unattend>
'@

# Boots one ISO in a new VM; screenshots every 30 s. Returns $true when the guest powered itself off
# within $minutes.
function Install([string] $run, [string] $isoFile, [int] $minutes) {
    $vmDir = Join-Path $Lab "vm\legacy-$run"
    $shots = Join-Path $Lab "out\vm-legacy-$run"
    foreach ($dir in $vmDir, $shots) { if (Test-Path $dir) { Remove-Item $dir -Recurse -Force } }
    New-Item -ItemType Directory -Force $vmDir, $shots | Out-Null
    $vmx = Join-Path $vmDir "wl-legacy-$run.vmx"
    Native { & $vdisk -c -s 64GB -a lsilogic -t 0 (Join-Path $vmDir 'disk.vmdk') } | Out-Null
    $vmxText = @"
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "21"
displayName = "WinLove legacy Setup lab ($run)"
guestOS = "windows11-64"
firmware = "efi"
uefi.secureBoot.enabled = "FALSE"
memsize = "4096"
numvcpus = "2"
sata0.present = "TRUE"
sata0:0.present = "TRUE"
sata0:0.fileName = "disk.vmdk"
sata0:1.present = "TRUE"
sata0:1.deviceType = "cdrom-image"
sata0:1.fileName = "$isoFile"
ethernet0.present = "FALSE"
usb.present = "FALSE"
sound.present = "FALSE"
floppy0.present = "FALSE"
svga.graphicsMemoryKB = "262144"
RemoteDisplay.vnc.enabled = "TRUE"
RemoteDisplay.vnc.port = "$vncPort"
RemoteDisplay.vnc.ip = "127.0.0.1"
tools.upgrade.policy = "manual"
"@
    [System.IO.File]::WriteAllText($vmx, $vmxText, (New-Object System.Text.UTF8Encoding $false))
    $poweredOff = $false
    try {
        Say "`n> vmrun start $vmx nogui"
        Native { & $vmrun -T ws start $vmx nogui } | ForEach-Object { Say "  $_" }
        Check "VM '$run' started" (VmRunning $vmx)
        $deadline = (Get-Date).AddMinutes($minutes)
        $n = 0
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 30
            $n++
            Native { python (Join-Path $repo 'tools\vnc_shot.py') $vncPort (Join-Path $shots ('shot-{0:D3}.png' -f $n)) } | Out-Null
            if (-not (VmRunning $vmx)) { $poweredOff = $true; break }
        }
        Say ("  '$run': " + @(Get-ChildItem $shots -Filter 'shot-*.png').Count + " screenshot(s) in $shots, powered off: $poweredOff")
    } finally {
        if (VmRunning $vmx) { Native { & $vmrun -T ws stop $vmx hard } | Out-Null }
        if (-not $KeepVm) {
            Native { & $vmrun -T ws deleteVM $vmx } | Out-Null
            Remove-Item $vmDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    return $poweredOff
}

Say ("=== lab_legacy_setup " + (Get-Date -Format s))
try {
    # 1. The boot image.
    Run @('extract', $Iso, 'sources/boot.wim', $bootWim) | Out-Null
    Check 'boot.wim taken out of the ISO' ($script:lastExit -eq 0 -and (Test-Path $bootWim))
    $original = Join-Path $work 'boot-original.wim'
    Copy-Item $bootWim $original
    Run @('boot-patch', $bootWim, $bootMount, '--bypass=nonsense', '--legacy-setup') | Out-Null
    Check 'an unknown check is still refused' ($script:lastExit -ne 0)
    $out = Run @('boot-patch', $bootWim, $bootMount, '--legacy-setup', '--verbose')
    Check 'boot image patched (--legacy-setup alone)' ($script:lastExit -eq 0 -and [bool]($out -match 'previous Setup'))
    Run @('mount', $bootWim, '2', $bootMount) | Out-Null
    $mounted = $script:lastExit -eq 0
    Check 'mount the patched boot image' $mounted
    if ($mounted) {
        try {
            $value = CmdLine $bootMount
            Say "  CmdLine = $value"
            Check 'CmdLine starts the previous Setup' ($value -eq $expected)
            Check 'X:\sources\setup.exe is there' (Test-Path (Join-Path $bootMount 'sources\setup.exe'))
            Check 'wpeinit.exe is there' (Test-Path (Join-Path $bootMount 'Windows\System32\wpeinit.exe'))
        } finally {
            Run @('unmount', $bootMount, '--discard') | Out-Null
        }
    }
    Run @('mount', $original, '2', $bootMount) | Out-Null
    if ($script:lastExit -eq 0) {
        try { $value = CmdLine $bootMount; Say "  original CmdLine = $value"; Check 'the original boot image runs winpeshl.exe' ($value -eq 'winpeshl.exe') }
        finally { Run @('unmount', $bootMount, '--discard') | Out-Null }
    }
    Run @('verify', $bootWim) | Out-Null
    Check 'every stream of the patched boot.wim is sound' ($script:lastExit -eq 0)

    if ($Vm) {
        # 2. An install image without WinRE.
        Run @('export', (Join-Path $setup 'sources\install.wim'), "$Edition", "$work\install.wim") | Out-Null
        Run @('mount', "$work\install.wim", '1', $mount) | Out-Null
        Check "mount edition $Edition" ($script:lastExit -eq 0)
        $winre = Join-Path $mount 'Windows\System32\Recovery\Winre.wim'
        Check 'the edition has WinRE' (Test-Path $winre)
        Remove-Item $winre -Force
        Run @('unmount', $mount, '--commit') | Out-Null
        Check 'WinRE removed and committed' ($script:lastExit -eq 0)

        # 3. Two ISOs that differ only in boot.wim.
        $media = Join-Path $work 'media'
        Native { robocopy.exe $setup $media /E /XF install.wim boot.wim /NFL /NDL /NJH /NJS /NP } | Out-Null
        Move-Item "$work\install.wim" (Join-Path $media 'sources\install.wim')
        [System.IO.File]::WriteAllText((Join-Path $media 'autounattend.xml'), $unattend, (New-Object System.Text.UTF8Encoding $false))
        $isoNew = Join-Path $work 'wl-new.iso'
        $isoLegacy = Join-Path $work 'wl-legacy.iso'
        Copy-Item $original (Join-Path $media 'sources\boot.wim')
        Run @('iso', $media, $isoNew, '--label=WL_NEW', '--boot=uefi', '--no-prompt') | Out-Null
        Check 'ISO with the original boot.wim' ($script:lastExit -eq 0)
        Copy-Item $bootWim (Join-Path $media 'sources\boot.wim') -Force
        Run @('iso', $media, $isoLegacy, '--label=WL_LEGACY', '--boot=uefi', '--no-prompt') | Out-Null
        Check 'ISO with the patched boot.wim' ($script:lastExit -eq 0)
        Remove-Item $media -Recurse -Force

        $off = Install 'new' $isoNew $FailMinutes
        Check "new Setup without WinRE does not reach the desktop in $FailMinutes min (the failure, reproduced)" (-not $off)
        $off = Install 'legacy' $isoLegacy $TimeoutMinutes
        Check "previous Setup without WinRE installs to the desktop and shuts down within $TimeoutMinutes min" $off
    }
} catch {
    Say ("ERROR  " + $_.Exception.Message + " (line " + $_.InvocationInfo.ScriptLineNumber + ")")
    $failed++
} finally {
    foreach ($dir in $bootMount, $mount) {
        if (Test-Path (Join-Path $dir 'Windows')) { Run @('unmount', $dir, '--discard') | Out-Null }
    }
    if (-not $KeepVm) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
exit $failed
