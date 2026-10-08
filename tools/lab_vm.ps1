<#
.SYNOPSIS
  Generic install test in VMware: a WinLove changeset is applied to edition 4 of build\lab\setup
  (wlcli apply --commit), the image is installed unattended in a new VM (EFI, SATA 64 GB, 4 GB, no
  network, VNC on 127.0.0.1:5917) and the VM's own screen is captured every 30 s through VNC
  (tools\vnc_shot.py) into build\lab\out\vm-<Tag> until the guest shuts itself down after the first
  sign-in (Windows 11 opens Start on its own then). The host's screen is never captured.
  PASS = the guest powered itself off (that command only runs after a successful first sign-in).

    powershell -ExecutionPolicy Bypass -File tools\lab_vm.ps1 -Changes <changeset.json> -Tag start1   (elevated; ~40 min)

  -SourceWim <wim|esd>: edition -Edition of that image instead of build\lab\setup's (another Windows with
   this Setup, e.g. Windows 10 Pro from an ESD: -SourceWim win10.esd -Edition 7); the changeset applies.
  -InstallWim <wim> -ImageIndex N: install edition N of a ready install.wim (no changeset; AIO tests);
   -BootWim <wim>: the media's boot.wim replaced (e.g. one patched for the previous Setup);
   -SetupFolder <dir>: the setup media files from there instead of build\lab\setup (another Windows).
  -AnswerFile <xml>: that answer file instead of the lab's (e.g. one WinLove's P13 wrote; FirstLogon / ShutdownAfter do not apply).
  -NoBypass: the answer file leaves out the LabConfig TPM / Secure Boot / RAM bypasses (the VM has no
   TPM and no Secure Boot: does this media's Setup check Windows 11's requirements at all?).
  -Cpus / -MemMB: the vCPU count and memory (default 2, 4096). (An NVMe disk does not start under vmrun here.)
  -OpenThisPc: Explorer opens "This PC" at the first sign-in (icons tests).
  -Network: a NAT network card, e1000 (e1000e and vmxnet3 crash this VMware at power-on; Windows Update, Store and OOBE downloads happen; the default is none).
  -FirstLogon <command>: run at the first sign-in instead (e.g. a diagnostics script the changeset put
   into ProgramData); -ShutdownAfter <seconds> after that (default 150).
  -IsoArgs: more arguments for "wlcli iso" (D-080: --setup-du=<cab> --boot-files=<dir>).
  -LogDisk: only the second disk of -Diag (512 MB, label WLDIAG, no drive letter), collected into
   out\vm-<Tag>\diag afterwards - for what writes to it itself (lab_setup_logs.ps1); works with -InstallWim.
  -Diag: first-boot diagnostics (tools\vm_diag.ps1): the guest records what gets installed, by which
   process, and what lands on the desktop, from the specialize pass until -DiagMinutes after the first
   sign-in, onto a second virtual disk (a fixed VHD the host mounts afterwards) -> out\vm-<Tag>\diag.
   The guest shuts itself down when it is done.
  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe",
    [int] $Edition = 4,
    [int] $TimeoutMinutes = 80,
    [string] $Changes = '',
    [string] $Tag = 'test',
    [int] $VncPort = 5917,
    [string] $ProductKey = 'VK7JG-NPHTM-C97JM-9MPGT-3V66T', # generic install key of the edition (Pro; Home: YTMG3-N6DKC-DKB77-7M9GH-8HVX7)
    [switch] $OpenThisPc,
    [switch] $Network,
    [string] $FirstLogon = '',
    [int] $ShutdownAfter = 150,
    [switch] $Diag,
    [switch] $LogDisk,
    [int] $DiagMinutes = 30,
    [switch] $KeepVm,
    [int] $Cpus = 2,
    [int] $MemMB = 4096,
    [string] $InstallWim = '',
    [string] $SourceWim = '',
    [int] $ImageIndex = 1,
    [string] $BootWim = '',
    [string] $SetupFolder = '',
    [switch] $NoBypass,
    [string] $AnswerFile = '',
    [string[]] $IsoArgs = @()
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$setup = if ($SetupFolder) { [System.IO.Path]::GetFullPath($SetupFolder) } else { Join-Path $Lab 'setup' }
$work = Join-Path $Lab "work\vm-$Tag"
$mount = Join-Path $Lab "mount\vm-$Tag"
$vmDir = Join-Path $Lab "vm\$Tag"
$shots = Join-Path $Lab "out\vm-$Tag"
$log = Join-Path $Lab "out\vm-$Tag-test.log"
$vmware = 'C:\Program Files (x86)\VMware\VMware Workstation'
$vmrun = Join-Path $vmware 'vmrun.exe'
$vdisk = Join-Path $vmware 'vmware-vdiskmanager.exe'
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if ($Changes -and -not (Test-Path $Changes)) { throw "no changeset: $Changes" }
# -Diag writes its script into the image it mounts; a ready -InstallWim is never mounted, so the
# guest would never run it nor shut itself down (2026-10-07). Use -ShutdownAfter there.
if ($Diag -and $InstallWim) { throw '-Diag needs the image mounted: not with -InstallWim (use -ShutdownAfter <seconds>)' }
foreach ($need in $Cli, $vmrun, $vdisk, (Join-Path $setup 'sources\install.wim'), (Join-Path $setup 'efi\microsoft\boot\efisys_noprompt.bin')) {
    if (-not (Test-Path $need)) { throw "missing: $need" }
}
foreach ($dir in 'out', 'mount', 'vm') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
if (Test-Path (Join-Path $mount 'Windows')) { & $Cli unmount $mount --discard | Out-Null }
foreach ($dir in $work, $vmDir, $shots) { if (Test-Path $dir) { Remove-Item $dir -Recurse -Force } }
New-Item -ItemType Directory -Force $work, $mount, $vmDir, $shots | Out-Null
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
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export|iso)\s+)?\d+%\s*$' } | Select-Object -Last 8 | ForEach-Object { Say ("  " + $_.TrimEnd()) }
    Say ("  (exit $script:lastExit)")
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }
function VmRunning([string] $vmx) { (@(Native { & $vmrun -T ws list }) -join "`n") -match [regex]::Escape($vmx) }

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
        <InstallFrom><MetaData wcm:action="add"><Key>/IMAGE/INDEX</Key><Value>IMAGEINDEX</Value></MetaData></InstallFrom>
      </OSImage></ImageInstall>
      <UserData><AcceptEula>true</AcceptEula><ProductKey><Key>PRODUCTKEY</Key><WillShowUI>Never</WillShowUI></ProductKey></UserData>
    </component>
  </settings>
  <settings pass="specialize">
    <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <ComputerName>WL-ICONS</ComputerName>
    </component>
    <component name="Microsoft-Windows-Deployment" processorArchitecture="amd64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
      <RunSynchronous>
        <RunSynchronousCommand wcm:action="add"><Order>1</Order><Path>reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\OOBE /v BypassNRO /t REG_DWORD /d 1 /f</Path></RunSynchronousCommand>
SPECIALIZE
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
        <SynchronousCommand wcm:action="add"><Order>1</Order><CommandLine>OPENTHISPC</CommandLine></SynchronousCommand>
        <SynchronousCommand wcm:action="add"><Order>2</Order><CommandLine>SHUTDOWNCMD</CommandLine></SynchronousCommand>
      </FirstLogonCommands>
    </component>
  </settings>
</unattend>
'@

$unattend = $unattend.Replace('PRODUCTKEY', $ProductKey).Replace('IMAGEINDEX', "$ImageIndex")
if ($NoBypass) { $unattend = [regex]::Replace($unattend, '(?s)\s*<RunSynchronous>\s*<RunSynchronousCommand[^>]*><Order>1</Order><Path>reg add HKLM\\SYSTEM\\Setup\\LabConfig.*?</RunSynchronous>', '') }
$first = if ($FirstLogon) { [System.Security.SecurityElement]::Escape($FirstLogon) } elseif ($OpenThisPc) { 'cmd /c start explorer.exe shell:MyComputerFolder' } else { 'cmd /c echo first sign-in' }
$shutdownCmd = if ($Diag -or $ShutdownAfter -le 0) { 'cmd /c echo the guest shuts itself down' } else { "cmd /c timeout /t $ShutdownAfter /nobreak &amp; shutdown /s /t 0" }
$specialize = if ($Diag) { '        <RunSynchronousCommand wcm:action="add"><Order>2</Order><Path>cmd /c C:\ProgramData\WinLoveDiag\setup.cmd</Path></RunSynchronousCommand>' } else { '' }
$unattend = $unattend.Replace('OPENTHISPC', $first).Replace('SHUTDOWNCMD', $shutdownCmd).Replace("SPECIALIZE`r`n", "$specialize`r`n").Replace("SPECIALIZE`n", "$specialize`n")
Say ("=== lab_vm $Tag " + (Get-Date -Format s))
$vmx = Join-Path $vmDir "wl-$Tag.vmx"
try {
    # 1. The patched image (or a ready one: -InstallWim, installed as it is).
    if ($InstallWim) {
        Copy-Item $InstallWim "$work\install.wim"
        Check "install.wim taken as it is: $InstallWim (index $ImageIndex)" (Test-Path "$work\install.wim")
    } else {
    $from = if ($SourceWim) { $SourceWim } else { Join-Path $setup 'sources\install.wim' }
    Run @('export', $from, "$Edition", "$work\install.wim")
    Run @('mount', "$work\install.wim", '1', $mount)
    Check "mount edition $Edition" ($script:lastExit -eq 0)
    }
    if ($Changes -and -not $InstallWim) {
        Run @('apply', $Changes, $mount)
        Check "changeset applied: $Changes" ($script:lastExit -eq 0)
    }
    if ($Diag -and -not $InstallWim) {
        $setupCmd = "@echo off`r`nset D=C:\ProgramData\WinLoveDiag`r`n" +
            "auditpol /set /subcategory:{0CCE922B-69AE-11D9-BED3-505054503030} /success:enable > `"%D%\setup.txt`" 2>&1`r`n" +
            "reg add HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System\Audit /v ProcessCreationIncludeCmdLine_Enabled /t REG_DWORD /d 1 /f >> `"%D%\setup.txt`" 2>&1`r`n" +
            "wevtutil sl Security /ms:268435456 >> `"%D%\setup.txt`" 2>&1`r`n" +
            "wevtutil sl Microsoft-Windows-TaskScheduler/Operational /e:true >> `"%D%\setup.txt`" 2>&1`r`n" +
            "wevtutil sl Microsoft-Windows-AppXDeploymentServer/Operational /ms:67108864 >> `"%D%\setup.txt`" 2>&1`r`n" +
            "schtasks /create /tn WinLoveDiag /ru SYSTEM /sc onstart /rl highest /f /tr `"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File C:\ProgramData\WinLoveDiag\diag.ps1 -DiagMinutes $DiagMinutes`" >> `"%D%\setup.txt`" 2>&1`r`n" +
            "schtasks /run /tn WinLoveDiag >> `"%D%\setup.txt`" 2>&1`r`nexit /b 0`r`n"
        $diagOps = @{ format = 'winlove.changeset'; version = 1; operations = @(
            @{ kind = 'writeFile'; target = 'ProgramData\WinLoveDiag\diag.ps1'; value = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'vm_diag.ps1')); risk = 'low' },
            @{ kind = 'writeFile'; target = 'ProgramData\WinLoveDiag\setup.cmd'; value = $setupCmd; risk = 'low' }) }
        $diagJson = Join-Path $work 'diag-changes.json'
        [System.IO.File]::WriteAllText($diagJson, ($diagOps | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding $false))
        Run @('apply', $diagJson, $mount)
        Check 'diagnostics put into the image' ($script:lastExit -eq 0 -and (Test-Path (Join-Path $mount 'ProgramData\WinLoveDiag\setup.cmd')))
    }
    if (-not $InstallWim) {
        Run @('unmount', $mount, '--commit')
        Check 'committed' ($script:lastExit -eq 0)
    }

    # 2. Setup folder + ISO.
    $media = Join-Path $work 'media'
    Native { robocopy.exe $setup $media /E /XF install.wim /NFL /NDL /NJH /NJS /NP } | Out-Null
    Move-Item "$work\install.wim" (Join-Path $media 'sources\install.wim')
    if ($BootWim) { Copy-Item $BootWim (Join-Path $media 'sources\boot.wim') -Force }
    if ($AnswerFile) {
        # An answer file WinLove wrote (P13): it has to wipe disk 0 and install edition 1 itself.
        Copy-Item $AnswerFile (Join-Path $media 'autounattend.xml') -Force
        Check "answer file taken as it is: $AnswerFile" (Test-Path (Join-Path $media 'autounattend.xml'))
    } else {
    [System.IO.File]::WriteAllText((Join-Path $media 'autounattend.xml'), $unattend, (New-Object System.Text.UTF8Encoding $false))
    }
    Run (@('iso', $media, "$work\wl-$Tag.iso", '--label=WL_LAB', '--boot=uefi', '--no-prompt') + $IsoArgs)
    Check 'ISO built' ($script:lastExit -eq 0 -and (Test-Path "$work\wl-$Tag.iso"))

    # 3. The VM.
    Native { & $vdisk -c -s 64GB -a lsilogic -t 0 (Join-Path $vmDir 'disk.vmdk') } | Out-Null
    $diagVhd = Join-Path $vmDir 'diag.vhd'
    $diagDisk = ''
    if ($Diag -or $LogDisk) {
        # A fixed VHD is raw sectors + a 512-byte footer: VMware sees the sectors as a flat extent, the
        # host mounts the VHD afterwards. GPT "no drive letter" attribute: Setup and the host leave it alone.
        $dp = Join-Path $work 'diag-diskpart.txt'
        Set-Content -Path $dp -Encoding ASCII -Value @(
            "create vdisk file=`"$diagVhd`" maximum=512 type=fixed", "select vdisk file=`"$diagVhd`"", 'attach vdisk', 'convert gpt',
            'create partition primary', 'gpt attributes=0x8000000000000000', 'format fs=ntfs label=WLDIAG quick', 'detach vdisk')
        Native { diskpart.exe /s $dp } | Out-Null
        Set-Content -Path (Join-Path $vmDir 'diag.vmdk') -Encoding ASCII -Value @(
            '# Disk DescriptorFile', 'version=1', 'encoding="UTF-8"', 'CID=fffffffe', 'parentCID=ffffffff', 'createType="monolithicFlat"', '',
            'RW 1048576 FLAT "diag.vhd" 0', '', 'ddb.virtualHWVersion = "21"', 'ddb.geometry.cylinders = "1024"', 'ddb.geometry.heads = "16"',
            'ddb.geometry.sectors = "63"', 'ddb.adapterType = "lsilogic"')
        Check 'diag disk created' ((Test-Path $diagVhd) -and (Get-Item $diagVhd).Length -eq 536871424)
        $diagDisk = "sata0:2.present = `"TRUE`"`r`nsata0:2.fileName = `"diag.vmdk`""
    }
    $vmxText = @"
.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "21"
displayName = "WinLove lab $Tag"
guestOS = "windows11-64"
firmware = "efi"
uefi.secureBoot.enabled = "FALSE"
memsize = "$MemMB"
numvcpus = "$Cpus"
sata0.present = "TRUE"
sata0:0.present = "TRUE"
sata0:0.fileName = "disk.vmdk"
sata0:1.present = "TRUE"
sata0:1.deviceType = "cdrom-image"
sata0:1.fileName = "$work\wl-$Tag.iso"
NETWORKCARD
DIAGDISK
usb.present = "FALSE"
sound.present = "FALSE"
floppy0.present = "FALSE"
svga.graphicsMemoryKB = "262144"
RemoteDisplay.vnc.enabled = "TRUE"
RemoteDisplay.vnc.port = "$vncPort"
RemoteDisplay.vnc.ip = "127.0.0.1"
tools.upgrade.policy = "manual"
logging = "TRUE"
"@
    $card = if ($Network) { "ethernet0.present = `"TRUE`"`r`nethernet0.connectionType = `"nat`"`r`nethernet0.virtualDev = `"e1000`"`r`nethernet0.addressType = `"generated`"" } else { 'ethernet0.present = "FALSE"' }
    $vmxText = $vmxText.Replace('NETWORKCARD', $card).Replace('DIAGDISK', $diagDisk)
    [System.IO.File]::WriteAllText($vmx, $vmxText, (New-Object System.Text.UTF8Encoding $false))
    Say "`n> vmrun start $vmx nogui"
    Native { & $vmrun -T ws start $vmx nogui } | ForEach-Object { Say "  $_" }
    Check 'VM started' (VmRunning $vmx)
    if (-not (VmRunning $vmx)) { throw 'the VM did not start (see vmware.log / vmware-vmx.dmp in the VM folder)' }

    # 4. Watch it: a screenshot every 30 s until the guest powers itself off.
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    $n = 0
    $poweredOff = $false
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 30
        $n++
        $png = Join-Path $shots ('shot-{0:D3}.png' -f $n)
        Native { python (Join-Path $repo 'tools\vnc_shot.py') $vncPort $png } | Out-Null
        if (-not (VmRunning $vmx)) { $poweredOff = $true; break }
    }
    $taken = @(Get-ChildItem $shots -Filter 'shot-*.png')
    Say ("screenshots: " + $taken.Count + " (" + $shots + ")")
    # A guest that powers itself off leaves an ACPI soft-off in vmware.log; a VM stopped from outside
    # (vmrun stop by hand) does not - that must never read as a pass.
    $vmLogs = @(Get-ChildItem $vmDir -Filter 'vmware*.log' -ErrorAction SilentlyContinue)
    $vmLogs | Copy-Item -Destination $shots -ErrorAction SilentlyContinue # kept: the VM folder goes
    $byGuest = $poweredOff -and ($vmLogs.Count -eq 0 -or [bool]($vmLogs | Select-String -Pattern 'Soft Off' -SimpleMatch -Quiet))
    if ($poweredOff -and $vmLogs.Count -eq 0) { Say 'no vmware.log: who powered the VM off cannot be told' }
    if ($poweredOff -and -not $byGuest) { Say 'the VM went off without an ACPI soft-off in vmware.log: stopped from outside' }
    Check "the guest reached the desktop and shut itself down within $TimeoutMinutes min (no boot loop)" $byGuest
    if (($Diag -or $LogDisk) -and $poweredOff) {
        $image = Mount-DiskImage -ImagePath $diagVhd -NoDriveLetter -PassThru
        $part = Get-Partition -DiskNumber ($image | Get-Disk).Number | Where-Object { $_.Type -eq 'Basic' } | Select-Object -First 1
        $at = Join-Path $work 'diag-mount'
        New-Item -ItemType Directory -Force $at | Out-Null
        Add-PartitionAccessPath -DiskNumber $part.DiskNumber -PartitionNumber $part.PartitionNumber -AccessPath ($at + '\')
        Native { robocopy.exe $at (Join-Path $shots 'diag') /E /XD 'System Volume Information' /R:0 /W:0 /NFL /NDL /NJH /NJS /NP } | Out-Null
        Remove-PartitionAccessPath -DiskNumber $part.DiskNumber -PartitionNumber $part.PartitionNumber -AccessPath ($at + '\')
        Dismount-DiskImage -ImagePath $diagVhd | Out-Null
        Check "diagnostics collected ($shots\diag)" ($LogDisk -or (Test-Path (Join-Path $shots 'diag\done.txt')))
    }
} catch {
    Say ("ERROR  " + $_.Exception.Message + " (line " + $_.InvocationInfo.ScriptLineNumber + ")")
    $failed++
} finally {
    if (Test-Path (Join-Path $mount 'Windows')) { Run @('unmount', $mount, '--discard') }
    if ((Test-Path $vmx) -and (VmRunning $vmx)) { Native { & $vmrun -T ws stop $vmx hard } | Out-Null }
    if (($Diag -or $LogDisk) -and (Test-Path $diagVhd) -and (Get-DiskImage -ImagePath $diagVhd -ErrorAction SilentlyContinue).Attached) {
        Dismount-DiskImage -ImagePath $diagVhd | Out-Null
    }
    if (-not $KeepVm) {
        if (Test-Path $vmx) { Native { & $vmrun -T ws deleteVM $vmx } | Out-Null }
        Remove-Item $vmDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
