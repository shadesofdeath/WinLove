<#
.SYNOPSIS
  D-134: drivers in Setup's own image (boot.wim), proven on the real thing. Everything happens under
  build\lab\bootdrv (the lab's setup files and this PC's Windows are only read):

  1. Engine (about 6 minutes): a copy of build\lab\setup\sources\boot.wim gets real third-party
     drivers through "wlcli boot-patch": VMware's pvscsi (from VMware Workstation's windows.iso, read
     with tar), this PC's storage controller and wired network card (wlcli export-host-drivers), plus
     four that must NOT go in: an ARM64-only INF, one without its catalog, one whose catalog does not
     match (a storage driver: DISM refuses it) and a changed network INF (DISM takes it "unsigned":
     WinLove takes it out again). DISM's own listing of both indexes (dism /Get-Drivers), wlcli's
     file-list reading (media-drivers), boot index, stream check (verify).
  2. -Vm (about an hour, VMware): a VM whose only disk hangs on a VMware Paravirtual SCSI controller.
     Windows 11 has pvscsi in the box, so the negative control removes that inbox driver from a copy
     of boot.wim (index 2); the ISO of the negative run has nothing else; the positive run's ISO is
     made by "wlcli iso --boot-driver=<VMware pvscsi.inf>" from the very same copy. Expected: no
     disk / Setup stops (negative), Setup installs and the guest reaches the desktop (positive).
     VNC frames: build\lab\bootdrv\out\vm-bd-neg|bd-pos. -SkipNegative runs only the positive one.

    powershell -ExecutionPolicy Bypass -File tools\lab_boot_drivers.ps1 [-Vm] [-SkipNegative]   (elevated)
  Log: build\lab\bootdrv\out\boot-drivers-test.log (-Vm: boot-drivers-all-test.log, -Vm -SkipEngine:
  boot-drivers-vm-test.log). wlcli is copied into build\lab\bootdrv\bin first:
  building meanwhile does not touch the run.
  Never runs "wlcli cleanup" / dism /Cleanup-Mountpoints: other tools' mounts stay as they are
  (listed before and after).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $Root = (Join-Path $PSScriptRoot '..\build\lab\bootdrv'),
    [string] $SetupFolder = (Join-Path $PSScriptRoot '..\build\lab\setup'),
    [string] $CliSource = (Join-Path $PSScriptRoot '..\build\x64-debug\bin\wlcli.exe'),
    [string] $VmwareTools = 'C:\Program Files (x86)\VMware\VMware Workstation\windows.iso',
    [switch] $Vm,
    [switch] $SkipNegative,
    [switch] $SkipEngine,
    [int] $VncPort = 5931,
    [int] $Edition = 4,
    [string] $LogName = ''
)
$ErrorActionPreference = 'Stop'
$Root = [System.IO.Path]::GetFullPath($Root)
$SetupFolder = [System.IO.Path]::GetFullPath($SetupFolder)
$cli = Join-Path $Root 'bin\wlcli.exe'
if (-not $LogName) { $LogName = if ($Vm -and $SkipEngine) { 'boot-drivers-vm-test.log' } elseif ($Vm) { 'boot-drivers-all-test.log' } else { 'boot-drivers-test.log' } }
$log = Join-Path $Root "out\$LogName"
$mnt = Join-Path $Root 'mount\pe'
$wim = Join-Path $Root 'work\boot.wim'
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
foreach ($need in $CliSource, (Join-Path $SetupFolder 'sources\boot.wim')) { if (-not (Test-Path $need)) { throw "missing: $need" } }
foreach ($dir in 'bin', 'out', 'work', 'drivers', 'mount\pe') { New-Item -ItemType Directory -Force (Join-Path $Root $dir) | Out-Null }
Copy-Item $CliSource $cli -Force
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
    $output = @(Native { & $cli @arguments })
    $script:lastExit = $LASTEXITCODE
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|discard|commit|verify|boot|extract|unmount)\s+)?[\d.]+(%| GB)\s*$' } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*((verify|boot|extract|mount|commit|discard)\s+)?[\d.]+%)+', '').TrimEnd()) }
    Say ("  (exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $output
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }
function DismDrivers([string] $label) {
    Say "`n> dism /English /Image:$mnt /Get-Drivers   ($label)"
    $out = @(Native { dism.exe /English "/Image:$mnt" /Get-Drivers })
    $out | Where-Object { $_.Trim() -and $_ -notmatch '^(Deployment Image|Version:|Image Version|Obtaining)' } | ForEach-Object { Say ("  " + $_.TrimEnd()) }
    return $out
}
# Mounts `image` index `index` read-only (or read-write) at $mnt, runs `body`, and always unmounts:
# discarded unless -Commit.
function Mounted([string] $image, [int] $index, [scriptblock] $body, [switch] $Write, [switch] $Commit) {
    $arguments = @('mount', $image, "$index", $mnt)
    if (-not $Write) { $arguments += '--readonly' }
    Run $arguments | Out-Null
    $ok = $script:lastExit -eq 0
    Check "mount $(Split-Path $image -Leaf) index $index" $ok
    $saved = $false
    try { if ($ok) { & $body; $saved = $Commit } }
    finally {
        if ($ok) {
            $how = if ($saved) { '--commit' } else { '--discard' }
            Run @('unmount', $mnt, $how) | Out-Null
            Check "unmount index $index ($how)" ($script:lastExit -eq 0)
        }
    }
}
function OemNames([object[]] $dismOut) { @($dismOut | Where-Object { $_ -match 'Original File Name : (.+)$' } | ForEach-Object { $Matches[1].Trim() }) }

Say ("=== WinLove boot.wim drivers lab (D-134) - " + (Get-Date -Format s))
Run @('mounts') | Out-Null
$mountsBefore = @(Native { & $cli mounts })
$pv = Join-Path $Root 'drivers\vmware\Program Files\VMware\VMware Tools\Drivers\pvscsi\Win10\amd64\pvscsi.inf'
try {
    # ---- the drivers -------------------------------------------------------------------------
    if (-not (Test-Path $pv)) {
        if (-not (Test-Path $VmwareTools)) { throw "VMware Tools ISO not found: $VmwareTools" }
        $vmw = Join-Path $Root 'drivers\vmware'
        New-Item -ItemType Directory -Force $vmw | Out-Null
        Native { tar.exe -xf $VmwareTools -C $vmw 'Program Files/VMware/VMware Tools/Drivers/pvscsi/Win10/amd64' } | Out-Null
    }
    Check 'VMware pvscsi driver (windows.iso, Win10\amd64)' (Test-Path $pv)
    $hostDir = Join-Path $Root 'drivers\host'
    if (-not (Test-Path $hostDir)) {
        Run @('export-host-drivers', $hostDir) | Out-Null
        Check 'this PC''s drivers exported' ($script:lastExit -eq 0)
    }
    # What the Drivers page's boot tab picks from them: storage + wired network.
    $scan = (Native { & $cli driver-scan $hostDir --arch=x64 --json } | Out-String) | ConvertFrom-Json
    $picked = @($scan | Where-Object { $_.setup -eq 'pick' })
    Say ("  this PC's pick for the setup media: " + (($picked | ForEach-Object { (Split-Path $_.inf -Leaf) + ' (' + $_.role + ')' }) -join ', '))
    $storage = @($picked | Where-Object { $_.role -eq 'storage' } | Select-Object -First 1)
    $wired = @($picked | Where-Object { $_.role -eq 'network' } | Select-Object -First 1)
    Check 'a storage driver of this PC is picked' ($storage.Count -eq 1)
    Check 'no Wi-Fi driver is picked (Windows PE has no WLAN)' (@($picked | Where-Object { $_.ifType -eq 71 }).Count -eq 0)
    $hostStorage = if ($storage.Count) { $storage[0].inf } else { '' }
    $hostNet = if ($wired.Count) { $wired[0].inf } else { '' }

    # Four that must not go in, made from real packages (in their own folders).
    $tests = Join-Path $Root 'drivers\tests'
    if (Test-Path $tests) { Remove-Item $tests -Recurse -Force }
    $pvDir = Split-Path $pv
    $unsigned = Join-Path $tests 'unsigned\pvscsi.inf'; New-Item -ItemType Directory -Force (Split-Path $unsigned) | Out-Null
    Copy-Item (Join-Path $pvDir 'pvscsi.inf'), (Join-Path $pvDir 'pvscsi.sys') (Split-Path $unsigned)
    $tampered = Join-Path $tests 'tampered\pvscsi.inf'; New-Item -ItemType Directory -Force (Split-Path $tampered) | Out-Null
    Copy-Item (Join-Path $pvDir '*') (Split-Path $tampered)
    Add-Content -Path $tampered -Value '; changed by the WinLove lab: the catalog no longer matches' -Encoding ASCII
    $arm = Join-Path $tests 'arm64\pvscsi.inf'; New-Item -ItemType Directory -Force (Split-Path $arm) | Out-Null
    Copy-Item (Join-Path $pvDir '*') (Split-Path $arm)
    $text = [IO.File]::ReadAllText($arm).Replace('NTx86,NTamd64.10.0...20124', 'NTarm64.10.0...20124').Replace('[pvscsi.NTx86]', '').Replace('[pvscsi.NTamd64.10.0...20124]', '[pvscsi.NTarm64.10.0...20124]')
    [IO.File]::WriteAllText($arm, $text, [Text.Encoding]::ASCII)
    $netTampered = ''
    if ($hostNet) {
        $netTampered = Join-Path $tests ('net\' + (Split-Path $hostNet -Leaf))
        New-Item -ItemType Directory -Force (Split-Path $netTampered) | Out-Null
        Copy-Item (Join-Path (Split-Path $hostNet) '*') (Split-Path $netTampered) -Recurse
        Add-Content -Path $netTampered -Value '; changed by the WinLove lab: the catalog no longer matches' -Encoding ASCII
    }

    if (-not $SkipEngine) {
    # ---- 1. engine -----------------------------------------------------------------------------
    Copy-Item (Join-Path $SetupFolder 'sources\boot.wim') $wim -Force
    Set-ItemProperty $wim -Name IsReadOnly -Value $false
    $sizeBefore = (Get-Item $wim).Length
    $info = (Native { & $cli info $wim --json } | Out-String) | ConvertFrom-Json
    $bootIndex = $info.install.bootIndex
    Say "  boot.wim: $(@($info.install.images).Count) image(s), boot index $bootIndex, $sizeBefore bytes"

    $arguments = @('boot-patch', $wim, $mnt, "--driver=$pv", "--driver=$pv", "--driver=$arm", "--driver=$unsigned", "--driver=$tampered")
    if ($hostStorage) { $arguments += "--driver=$hostStorage" }
    if ($hostNet) { $arguments += "--driver=$hostNet" }
    if ($netTampered) { $arguments += "--driver=$netTampered" }
    $out = Run ($arguments + '--verbose')
    Check 'boot-patch: exit 3 (some drivers refused, the rest went in)' ($script:lastExit -eq 3)
    $expectAdded = 1 + [int][bool]$hostStorage + [int][bool]$hostNet
    Check "boot-patch: $expectAdded driver(s) added" ([bool]($out -match " $expectAdded driver\(s\) added"))
    Check 'the same INF given twice is added once' ([bool]($out -match 'listed twice, added once'))
    Check 'ARM64-only INF refused before DISM (architecture)' ([bool]($out -match 'driver not added: pvscsi\.inf: built for arm64 only'))
    Check 'INF without its catalog refused before DISM' ([bool]($out -match 'driver not added: pvscsi\.inf: unsigned \(its catalog pvscsi\.cat is not next to it\)'))
    Check 'storage INF with a catalog that does not match: DISM refuses, the reason is said' ([bool]($out -match 'unsigned boot-critical driver'))
    if ($netTampered) { Check 'network INF DISM takes as unsigned: taken out again' ([bool]($out -match 'driver taken out again: DISM lists oem\d+\.inf .* as unsigned')) }
    Check 'both catalog cases read the same: "unsigned: its catalog does not match"' (@($out | Where-Object { $_ -match 'driver not added: .*unsigned: its catalog does not match' }).Count -eq $(if ($netTampered) { 2 } else { 1 }))

    $out = Run @('boot-patch', $wim, $mnt, "--driver=$pv")
    Check 'a package the image has already: "already in the image", exit 0' ($script:lastExit -eq 0 -and [bool]($out -match 'driver already in the image'))

    Mounted $wim $bootIndex {
        $d = DismDrivers "after, index $bootIndex (Setup)"
        $names = OemNames $d
        Say ("  DISM, index ${bootIndex}: " + ($names -join ', '))
        Check "index ${bootIndex}: DISM lists pvscsi.inf" ($names -contains 'pvscsi.inf')
        if ($hostStorage) { Check "index ${bootIndex}: DISM lists $(Split-Path $hostStorage -Leaf)" ($names -contains (Split-Path $hostStorage -Leaf).ToLower()) }
        if ($hostNet) { Check "index ${bootIndex}: DISM lists $(Split-Path $hostNet -Leaf)" ($names -contains (Split-Path $hostNet -Leaf).ToLower()) }
        Check "index ${bootIndex}: exactly $expectAdded third-party package(s) (no ARM64 / unsigned one)" ($names.Count -eq $expectAdded)
        $json = (Native { & $cli drivers $mnt --json } | Out-String) | ConvertFrom-Json
        Check "index ${bootIndex}: every package signed (wlcli drivers)" (@($json | Where-Object { -not $_.signed }).Count -eq 0)
    }
    foreach ($other in @($info.install.images | Where-Object { $_.index -ne $bootIndex })) {
        Mounted $wim $other.index {
            $d = DismDrivers "after, index $($other.index)"
            Say ("  index $($other.index) (" + $other.name + "): " + (OemNames $d).Count + ' third-party driver(s): Setup does not boot it')
        }
    }
    $media = (Native { & $cli media-drivers $wim --json } | Out-String) | ConvertFrom-Json
    Say ("  wlcli media-drivers (file lists, no mount): " + (($media.install | ForEach-Object { $_.publishedName + '=' + $_.originalName }) -join ', '))
    Check 'the file-list reading finds the same packages by their original names' (@($media.install).Count -eq $expectAdded -and (@($media.install | ForEach-Object originalName) -contains 'pvscsi.inf'))
    $after = (Native { & $cli info $wim --json } | Out-String) | ConvertFrom-Json
    Check 'still the same images' (@($after.install.images).Count -eq @($info.install.images).Count)
    Check 'boot index unchanged' ($after.install.bootIndex -eq $bootIndex)
    Run @('verify', $wim) | Out-Null
    Check 'every stream of the patched boot.wim is sound' ($script:lastExit -eq 0)
    Say ("  size: $sizeBefore -> " + (Get-Item $wim).Length + " bytes")
    }

    # ---- 2. the VM -----------------------------------------------------------------------------
    if ($Vm) {
        # Windows 11 has pvscsi in the box (pvscsii.inf): the negative control takes it out of a copy.
        $stripped = Join-Path $Root 'work\boot-nopvscsi.wim'
        Copy-Item (Join-Path $SetupFolder 'sources\boot.wim') $stripped -Force
        Set-ItemProperty $stripped -Name IsReadOnly -Value $false
        $bi = ((Native { & $cli info $stripped --json } | Out-String) | ConvertFrom-Json).install.bootIndex
        Mounted $stripped $bi -Write -Commit {
            $dirs = @(Get-ChildItem (Join-Path $mnt 'Windows\System32\DriverStore\FileRepository') -Directory -Filter 'pvscsii.inf_*')
            $targets = @(Get-ChildItem (Join-Path $mnt 'Windows\INF') -Filter 'pvscsii.*') +
                @(Get-Item (Join-Path $mnt 'Windows\System32\drivers\pvscsii.sys') -ErrorAction SilentlyContinue)
            foreach ($d in $dirs) { $targets += @(Get-ChildItem $d.FullName -Recurse -File) }
            $targets += $dirs
            foreach ($t in $targets) {
                # takeown one item at a time: "/R /D Y" needs the UI language's answer letter ("E" in Turkish),
                # and icacls /setowner is refused here (lab 2026-11-10).
                Native { takeown.exe /F $t.FullName /A } | Out-Null
                Native { icacls.exe $t.FullName /grant '*S-1-5-32-544:F' /C /Q } | Out-Null
                try {
                    Remove-Item $t.FullName -Recurse -Force
                    Say ("  removed from the copy: " + $t.FullName.Substring($mnt.Length + 1))
                } catch {
                    Say ("  could not remove " + $t.FullName.Substring($mnt.Length + 1) + ": " + $_.Exception.Message)
                }
            }
            $left = @(Get-ChildItem (Join-Path $mnt 'Windows\INF') -Filter 'pvscsii.*') +
                @(Get-ChildItem (Join-Path $mnt 'Windows\System32\DriverStore\FileRepository') -Directory -Filter 'pvscsii.inf_*')
            $stripOk = $targets.Count -ge 3 -and $left.Count -eq 0 -and -not (Test-Path (Join-Path $mnt 'Windows\System32\drivers\pvscsii.sys'))
            Check 'inbox pvscsi driver taken out of the copy' $stripOk
            if (-not $stripOk) { throw 'the inbox pvscsi driver could not be taken out: no VM run (the copy is discarded)' }
        }
        $left = @(Native { & $cli wim-ls $stripped $bi 'Windows\INF' } | Where-Object { $_ -match 'pvscsi' })
        Check 'the copy has no pvscsi INF (file list)' ($left.Count -eq 0)
        $labVm = Join-Path $PSScriptRoot 'lab_vm.ps1'
        $common = @{ Lab = $Root; Cli = $cli; SetupFolder = $SetupFolder; InstallWim = (Join-Path $SetupFolder 'sources\install.wim');
                     ImageIndex = $Edition; BootWim = $stripped; DiskBus = 'pvscsi'; VncPort = $VncPort }
        if (-not $SkipNegative) {
            Say "`n> lab_vm.ps1 -Tag bd-neg -DiskBus pvscsi (boot.wim without any pvscsi driver)"
            try { & $labVm @common -Tag 'bd-neg' -TimeoutMinutes 12 -ShotSeconds 20 } catch { Say ("  lab_vm: " + $_.Exception.Message) }
            $negLog = Get-Content (Join-Path $Root 'out\vm-bd-neg-test.log') -ErrorAction SilentlyContinue
            Check 'negative control: ISO built' ([bool]($negLog -match 'PASS  ISO built'))
            Check 'negative control: the guest did NOT reach the desktop (Setup has no disk)' ([bool]($negLog -match 'FAIL  the guest reached the desktop'))
        }
        Say "`n> lab_vm.ps1 -Tag bd-pos -DiskBus pvscsi -IsoArgs --boot-driver=<VMware pvscsi.inf>"
        try { & $labVm @common -Tag 'bd-pos' -TimeoutMinutes 80 -ShotSeconds 30 -KeepVm -IsoArgs @("--boot-driver=$pv") } catch { Say ("  lab_vm: " + $_.Exception.Message) }
        $posLog = Get-Content (Join-Path $Root 'out\vm-bd-pos-test.log') -ErrorAction SilentlyContinue
        Check 'positive: ISO built with the driver in boot.wim' ([bool]($posLog -match 'PASS  ISO built'))
        Check 'positive: Setup installed Windows on the pvscsi disk, the guest reached the desktop' ([bool]($posLog -match 'PASS  the guest reached the desktop'))
        $iso = Join-Path $Root 'work\vm-bd-pos\wl-bd-pos.iso'
        if (Test-Path $iso) {
            $m = (Native { & $cli media-drivers $iso --json } | Out-String) | ConvertFrom-Json
            Say ("  the positive ISO's boot.wim [" + $m.bootIndex + "]: " + (($m.boot | ForEach-Object { $_.publishedName + '=' + $_.originalName + ' ' + $_.version }) -join ', '))
            Check 'the positive ISO carries pvscsi.inf in boot.wim''s Setup image' (@($m.boot | ForEach-Object originalName) -contains 'pvscsi.inf')
        }
        # -KeepVm kept the positive run's VM and work folder for the look above: remove them now.
        $vmx = Join-Path $Root 'vm\bd-pos\wl-bd-pos.vmx'
        $vmrun = 'C:\Program Files (x86)\VMware\VMware Workstation\vmrun.exe'
        if (Test-Path $vmx) { Native { & $vmrun -T ws stop $vmx hard } | Out-Null; Native { & $vmrun -T ws deleteVM $vmx } | Out-Null }
        Remove-Item (Join-Path $Root 'vm\bd-pos') -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item (Join-Path $Root 'work\vm-bd-pos') -Recurse -Force -ErrorAction SilentlyContinue
    }
} catch {
    Say ("ERROR  " + $_.Exception.Message + " (line " + $_.InvocationInfo.ScriptLineNumber + ")")
    $failed++
} finally {
    if (Test-Path (Join-Path $mnt 'Windows')) { Run @('unmount', $mnt, '--discard') | Out-Null }
    Run @('mounts') | Out-Null
    $mountsAfter = @(Native { & $cli mounts })
    Check 'the mounts are as they were before the lab' ((($mountsBefore | Where-Object { $_ -notmatch '^\s*$' }) -join "`n") -eq (($mountsAfter | Where-Object { $_ -notmatch '^\s*$' }) -join "`n"))
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
exit $failed
