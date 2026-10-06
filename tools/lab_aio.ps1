<#
.SYNOPSIS
  AIO install test (D-077): one install.wim with Windows 11 25H2 Pro (edition 4 of build\lab\setup)
  and Windows 10 22H2 Pro (from an ESD of the Media Creation Tool catalog), installed in VMware
  through tools\lab_vm.ps1 -InstallWim:
    w10-new     Windows 10 (index 2) with the Windows 11 25H2 boot.wim as it is (new Setup)
    w10-legacy  Windows 10 (index 2) with boot.wim patched for the previous Setup (wlcli boot-patch --legacy-setup)
    w11-new     Windows 11 (index 1) from the same AIO with the new Setup (the AIO breaks nothing)
    w10base-w10 / w10base-w11   the same AIO on Windows 10 setup media (built from the ESD: index 1 =
                the media files, 2 + 3 = boot.wim), installing Windows 10 / Windows 11
    swap-w10 / swap-w11   Windows 11 media + the AIO, its media replaced by wlcli setup-media (the
                app's way), installing Windows 10 / Windows 11
  PASS of a case = its guest reached the desktop and shut itself down.

    powershell -ExecutionPolicy Bypass -File tools\lab_aio.ps1 -Esd build\lab\win10\<file>.esd   (elevated; ~45 min)

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [Parameter(Mandatory = $true)] [string] $Esd,
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe",
    [string[]] $Cases = @('w10-new', 'w10-legacy', 'w11-new')
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$Esd = [System.IO.Path]::GetFullPath($Esd)
$aio = Join-Path $Lab 'aio'
$log = Join-Path $Lab 'out\aio-test.log'
$key = 'VK7JG-NPHTM-C97JM-9MPGT-3V66T' # generic Pro key: Windows 10 and 11
New-Item -ItemType Directory -Force (Join-Path $Lab 'out') | Out-Null
Remove-Item $log -ErrorAction SilentlyContinue
function Say([string] $text) { Add-Content -Path $log -Value $text -Encoding UTF8; Write-Host $text }
function Native([scriptblock] $command) {
    $previous = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $output = & $command 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = $previous
    return $output
}
$failed = 0
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }

Say ("=== lab_aio " + (Get-Date -Format s))
if (Test-Path $aio) { Remove-Item $aio -Recurse -Force }
New-Item -ItemType Directory -Force $aio | Out-Null
$wim = Join-Path $aio 'install.wim'

# 1. The AIO: Windows 11 Pro, then Windows 10 Pro out of the ESD.
Native { & $Cli export (Join-Path $Lab 'setup\sources\install.wim') 4 $wim } | Out-Null
Check 'Windows 11 Pro exported' ($LASTEXITCODE -eq 0)
$info = (Native { & $Cli info $Esd --json }) -join "`n" | ConvertFrom-Json
$w10 = @($info.install.images | Where-Object { $_.name -eq 'Windows 10 Pro' })
Check ('Windows 10 Pro found in the ESD (index ' + ($w10 | ForEach-Object index) + ')') ($w10.Count -eq 1)
Native { & $Cli append $Esd $wim "--index=$($w10[0].index)" } | ForEach-Object { Say "  $_" }
Check 'Windows 10 Pro appended' ($LASTEXITCODE -eq 0)
$after = (Native { & $Cli info $wim --json }) -join "`n" | ConvertFrom-Json
$after.install.images | ForEach-Object { Say ("  " + $_.index + "  " + $_.name + "  " + $_.version) }
Check 'AIO has 2 editions' ($after.install.imageCount -eq 2)

$copy = Join-Path $aio 'reorder-test.wim'
Copy-Item $wim $copy
Native { & $Cli reorder $copy '2,1' } | Out-Null
$swapped = (Native { & $Cli info $copy --json }) -join "`n" | ConvertFrom-Json
Check 'wlcli reorder 2,1: Windows 10 Pro first, Windows 11 Pro second' (
    $swapped.install.images[0].name -eq 'Windows 10 Pro' -and $swapped.install.images[1].name -eq 'Windows 11 Pro')
Remove-Item $copy

# 2. boot.wim for the previous Setup.
$legacyBoot = Join-Path $aio 'boot-legacy.wim'
Copy-Item (Join-Path $Lab 'setup\sources\boot.wim') $legacyBoot
$bootMount = Join-Path $Lab 'mount\aio-boot'
New-Item -ItemType Directory -Force $bootMount | Out-Null
Native { & $Cli boot-patch $legacyBoot $bootMount --legacy-setup } | ForEach-Object { Say "  $_" }
Check 'boot.wim patched for the previous Setup' ($LASTEXITCODE -eq 0)

# 2b. Windows 10 setup media out of the ESD (as the Media Creation Tool builds it): index 1 is the
# media's files, 2 + 3 the boot.wim (Windows PE, then Setup as the boot index).
$w10Media = Join-Path $aio 'w10media'
if (@($Cases | Where-Object { $_ -like 'w10base-*' }).Count -gt 0) {
    New-Item -ItemType Directory -Force $w10Media | Out-Null
    Native { dism.exe /Apply-Image /ImageFile:$Esd /Index:1 /ApplyDir:$w10Media } | Out-Null
    $applied = $LASTEXITCODE -eq 0
    $boot = Join-Path $w10Media 'sources\boot.wim'
    Native { dism.exe /Export-Image /SourceImageFile:$Esd /SourceIndex:2 /DestinationImageFile:$boot /Compress:max } | Out-Null
    $pe = $LASTEXITCODE -eq 0
    Native { dism.exe /Export-Image /SourceImageFile:$Esd /SourceIndex:3 /DestinationImageFile:$boot /Compress:max /Bootable } | Out-Null
    Check 'Windows 10 setup media built from the ESD' ($applied -and $pe -and $LASTEXITCODE -eq 0 -and
        (Test-Path (Join-Path $w10Media 'efi\microsoft\boot\efisys_noprompt.bin')))
    # lab_vm requires an install.wim in the setup folder; the AIO replaces it.
    Copy-Item $wim (Join-Path $w10Media 'sources\install.wim')
}

# 2c. The app's way (wlcli setup-media): Windows 11 media + the AIO, flagged, then its media swapped
# for Windows 10's straight out of the ESD.
$swap = Join-Path $aio 'swap'
if (@($Cases | Where-Object { $_ -like 'swap-*' }).Count -gt 0) {
    Native { robocopy.exe (Join-Path $Lab 'setup') $swap /E /XF install.wim /NFL /NDL /NJH /NJS /NP } | Out-Null
    Copy-Item $wim (Join-Path $swap 'sources\install.wim')
    Native { & $Cli media-check $swap } | ForEach-Object { Say "  $_" }
    Check 'media-check: Windows 10 cannot be installed from the 24H2 media (exit 1)' ($LASTEXITCODE -eq 1)
    Native { & $Cli setup-media $swap $Esd } | ForEach-Object { Say "  $_" }
    Check 'setup-media: Windows 10 media in place, every edition installable (exit 0)' ($LASTEXITCODE -eq 0)
    Check 'the AIO install.wim stayed' ((Get-Item (Join-Path $swap 'sources\install.wim')).Length -eq (Get-Item $wim).Length)
}

# 3. The installs, in parallel.
$runs = @{
    'w10-new'     = @('-ImageIndex', '2')
    'w10-legacy'  = @('-ImageIndex', '2', '-BootWim', $legacyBoot)
    'w11-new'     = @('-ImageIndex', '1')
    'w10base-w10' = @('-ImageIndex', '2', '-SetupFolder', $w10Media)
    'w10base-w11' = @('-ImageIndex', '1', '-SetupFolder', $w10Media)
    'swap-w10'    = @('-ImageIndex', '2', '-SetupFolder', $swap)
    'swap-w11'    = @('-ImageIndex', '1', '-SetupFolder', $swap)
}
$port = 5960
$procs = @()
foreach ($case in $Cases) {
    $port++
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'lab_vm.ps1'),
        '-Tag', "aio-$case", '-InstallWim', $wim, '-ProductKey', $key, '-VncPort', "$port", '-TimeoutMinutes', '60') + $runs[$case]
    $procs += Start-Process powershell.exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 20
}
$procs | Wait-Process
foreach ($case in $Cases) {
    $caseLog = Join-Path $Lab "out\vm-aio-$case-test.log"
    $ok = (Test-Path $caseLog) -and [bool](Select-String -Path $caseLog -Pattern '=== ALL PASSED' -Quiet)
    Check "$case reached the desktop (out\vm-aio-$case)" $ok
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
