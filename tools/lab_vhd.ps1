<#
.SYNOPSIS
  D-141 lab: a VHD / VHDX holding an installed (applied, never booted) Windows as a WinLove source.
  Everything happens under build\lab\vhd (inside the repo); only disks this script creates there are
  ever attached, and only through their own file path. No drive letter is left on the system, no
  other disk, mount or tool is touched (no cleanup command of any kind).

    -Phase Prepare    golden.vhdx (40 GB dynamic, GPT + one NTFS volume) with build\lab\setup\sources\
                      install.wim index 4 applied by dism /Apply-Image; detached again (~10 min, admin).
    -Phase Run        on a fresh copy work.vhdx: wlcli info / mount (changes disk) / read commands /
                      apply + discard / crash (killed apply) + recovery / simulated reboot + repair /
                      apply --commit (merge) + re-mount + verification + ScanHealth (~20 min, admin).
    -Phase VhdFormat  the same idea on a small .vhd (not .vhdx) with boot.wim index 1 (WinPE) applied.

  The wlcli under test is a copy in build\lab\vhd\bin (a build during the lab does not break it).
  Every attach is detached at the end, also on failure (finally). Logs: build\lab\vhd\lab-<phase>.log,
  outputs build\lab\vhd\out\<phase>\, done marker build\lab\vhd\lab-<phase>.done ("PASS" / "FAIL n").
  Plain ASCII on purpose (Windows PowerShell 5.1 reads a BOM-less script as ANSI).
#>
param(
    [ValidateSet('Prepare', 'Run', 'VhdFormat')] [string] $Phase = 'Run',
    [string] $Cli = ''
)
$ErrorActionPreference = 'Stop'
$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Lab = Join-Path $Root 'build\lab\vhd'
if (-not $Cli) { $Cli = Join-Path $Lab 'bin\wlcli.exe' }
$Wim = Join-Path $Root 'build\lab\setup\sources\install.wim'
$BootWim = Join-Path $Root 'build\lab\setup\sources\boot.wim'
$Golden = Join-Path $Lab 'golden.vhdx'
$Work = Join-Path $Lab 'work.vhdx'
$PeVhd = Join-Path $Lab 'pe.vhd'
$Mnt = Join-Path $Lab 'mnt'
$ChangesDir = Join-Path $Lab 'changes'
$Records = Join-Path $Lab 'records.json'
$tag = $Phase.ToLower()
$Out = Join-Path $Lab ('out\' + $tag)
$log = Join-Path $Lab ('lab-' + $tag + '.log')
$done = Join-Path $Lab ('lab-' + $tag + '.done')

New-Item -ItemType Directory -Force $Lab, $Out | Out-Null
Remove-Item $done -Force -ErrorAction SilentlyContinue
Set-Content -Path $log -Value ("=== lab_vhd " + $Phase + " " + (Get-Date -Format s)) -Encoding UTF8
$script:failed = 0

function Say([string] $text) { Add-Content -Path $log -Value $text -Encoding UTF8 }
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say ("PASS  " + $what) } else { Say ("FAIL  " + $what); $script:failed++ }
}
function Native([scriptblock] $command) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $command 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = $previous
    return $output
}
# wlcli with stdout / stderr into files (not -Wait: PS 5.1 then waits for the whole process tree).
function Invoke-Wl([string] $name, [string[]] $arguments) {
    $o = Join-Path $Out ($name + '.out.txt')
    $e = Join-Path $Out ($name + '.err.txt')
    $all = @($arguments) + @("--vhd-records=`"$Records`"")
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $Cli -ArgumentList ($all -join ' ') -RedirectStandardOutput $o -RedirectStandardError $e -NoNewWindow -PassThru
    $null = $p.Handle
    $p.WaitForExit()
    $code = $p.ExitCode
    $script:lastSeconds = [math]::Round($watch.Elapsed.TotalSeconds, 1)
    Say ("> wlcli " + ($arguments -join ' ') + "  -> exit $code, " + $script:lastSeconds + " s")
    Get-Content $o -ErrorAction SilentlyContinue | Where-Object { $_.Trim() -and $_ -notmatch '^\s*\r' } | Select-Object -Last 12 |
        ForEach-Object { Say ("    " + ($_ -replace '^.*\r', '').TrimEnd()) }
    return $code
}
function OutText([string] $name) { return (Get-Content (Join-Path $Out ($name + '.out.txt')) -Raw -ErrorAction SilentlyContinue) }
function Attached([string] $file) {
    if (-not (Test-Path $file)) { return $false }
    $image = Get-DiskImage -ImagePath $file -ErrorAction SilentlyContinue
    return [bool]($image -and $image.Attached)
}
function OurFiles() {
    $list = @()
    foreach ($f in @($Golden, $Work, $PeVhd)) { if (Test-Path $f) { $list += $f } }
    if (Test-Path $ChangesDir) { $list += @(Get-ChildItem $ChangesDir -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }) }
    return $list
}
function Snapshot([string] $name) {
    $file = Join-Path $Out ($name + '.txt')
    $lines = @('--- dism /Get-MountedImageInfo')
    $lines += @(Native { & dism.exe /English /Get-MountedImageInfo } | Where-Object { $_.Trim() })
    $lines += '--- Get-Disk'
    $lines += @(Get-Disk | Sort-Object Number | ForEach-Object { "{0} | {1} | {2} | {3} | {4}" -f $_.Number, $_.FriendlyName, $_.BusType, $_.PartitionStyle, $_.Size })
    $lines += '--- our files (attached?)'
    foreach ($f in OurFiles) { $lines += ("{0} attached={1} size={2}" -f $f, (Attached $f), (Get-Item $f).Length) }
    Set-Content -Path $file -Value $lines -Encoding UTF8
    return $file
}
function MountTable([string] $snapshotFile) {
    # The DISM part only, without the tool version line.
    $lines = Get-Content $snapshotFile
    $start = [array]::IndexOf($lines, '--- dism /Get-MountedImageInfo')
    $end = [array]::IndexOf($lines, '--- Get-Disk')
    return (($lines[($start + 1)..($end - 1)] | Where-Object { $_ -notmatch '^Version' }) -join "`n")
}
function DiskPart([string[]] $lines) {
    $file = Join-Path $Lab 'diskpart.txt'
    Set-Content -Path $file -Value $lines -Encoding ASCII
    $result = Native { diskpart.exe /s $file }
    Remove-Item $file -Force
    return $result
}
function IsReparse([string] $folder) {
    if (-not (Test-Path $folder)) { return $false }
    return [bool]((Get-Item $folder -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)
}
# A registry value of the mounted image (hive loaded under a lab name, unloaded again).
function ImageValue([string] $hive, [string] $key, [string] $value) {
    $file = Join-Path $Mnt ('Windows\System32\config\' + $hive)
    $name = 'WLVHDLAB_' + $hive
    $r = Native { reg.exe load "HKLM\$name" "$file" }
    if ($LASTEXITCODE -ne 0) { Say ("reg load failed: " + ($r -join ' ')); return '<load failed>' }
    $q = Native { reg.exe query "HKLM\$name\$key" /v $value }
    $found = if ($LASTEXITCODE -eq 0) { (($q | Where-Object { $_ -match $value }) -join ' ').Trim() } else { '<absent>' }
    $unloaded = $false
    for ($i = 0; $i -lt 10 -and -not $unloaded; $i++) {
        [gc]::Collect(); [gc]::WaitForPendingFinalizers()
        Native { reg.exe unload "HKLM\$name" } | Out-Null
        $unloaded = $LASTEXITCODE -eq 0
        if (-not $unloaded) { Start-Sleep -Seconds 1 }
    }
    if (-not $unloaded) { Say "WARNING: lab hive $name not unloaded" }
    return $found
}
function HiveList() {
    return @(Native { reg.exe query 'HKLM\SYSTEM\CurrentControlSet\Control\hivelist' } | Where-Object { $_ -match 'REG_SZ' } | ForEach-Object { $_.Trim() })
}
# Detach whatever of ours is still attached: through wlcli's own record first, then by file path.
function SafetyNet() {
    if (Test-Path $Records) {
        Say 'safety net: records left, unmount --discard through wlcli'
        [void](Invoke-Wl 'safety-unmount' @('unmount', "`"$Mnt`"", '--discard', '--verbose'))
    }
    if (IsReparse $Mnt) {
        Say 'safety net: removing the mount point on our folder'
        Native { mountvol.exe "$Mnt\" /D } | ForEach-Object { Say ("    " + $_) }
    }
    foreach ($f in OurFiles) {
        if (Attached $f) {
            Say ("safety net: detaching " + $f)
            Dismount-DiskImage -ImagePath $f -ErrorAction Continue | Out-Null
        }
    }
    if ((Test-Path $Mnt) -and -not (IsReparse $Mnt) -and @(Get-ChildItem $Mnt -Force -ErrorAction SilentlyContinue).Count -eq 0) {
        Remove-Item $Mnt -Force
    }
}

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { Say 'ABORT: not elevated'; Set-Content $done 'ABORT not elevated'; exit 9 }
$freeGb = [math]::Round((Get-PSDrive C).Free / 1GB, 1)
Say ("free on C: $freeGb GB")
Say ("cli: $Cli")

$result = 0
$before = Snapshot 'state-before'
try {
    if ($Phase -eq 'Prepare' -or $Phase -eq 'VhdFormat') {
        $isPe = $Phase -eq 'VhdFormat'
        $target = if ($isPe) { $PeVhd } else { $Golden }
        $sizeMb = if ($isPe) { 6144 } else { 40960 }
        $sourceWim = if ($isPe) { $BootWim } else { $Wim }
        $sourceIndex = if ($isPe) { 1 } else { 4 }
        if ($freeGb -lt 45) { throw "less than 45 GB free on C: ($freeGb GB)" }
        if (Test-Path $target) { throw "$target exists already (delete it by hand to prepare again)" }
        $apply = Join-Path $Lab 'apply'
        if (Test-Path $apply) { throw "$apply exists: an earlier prepare did not finish" }
        # Not $out: PowerShell names are case-insensitive, $Out is the output folder.
        $partOut = DiskPart @("create vdisk file=`"$target`" maximum=$sizeMb type=expandable")
        $partOut | ForEach-Object { Say ("    diskpart: " + $_) }
        Check "created $target ($sizeMb MB, expandable)" (Test-Path $target)
        $image = Mount-DiskImage -ImagePath $target -NoDriveLetter -PassThru
        $image = Get-DiskImage -ImagePath $target
        $disk = $image | Get-Disk
        Say ("attached as disk {0}: {1} {2} {3} bytes, style {4}, location {5}" -f $disk.Number, $disk.FriendlyName, $disk.BusType, $disk.Size, $disk.PartitionStyle, $disk.Location)
        # Only this exact disk: the number from our own file's image object, a file-backed disk,
        # empty (RAW) and of the size just created. Anything else and nothing is written.
        $mine = ($disk.Number -eq $image.Number) -and ($disk.BusType -eq 'File Backed Virtual') -and ($disk.PartitionStyle -eq 'RAW') -and ($disk.Location -eq $target) -and ([math]::Abs([double]$disk.Size - [double]$sizeMb * 1MB) -lt 64MB)
        Check 'the attached disk is our new, empty virtual disk' $mine
        if (-not $mine) { throw 'not our disk: nothing is written' }
        $disk = $disk | Initialize-Disk -PartitionStyle GPT -PassThru
        $part = $disk | New-Partition -UseMaximumSize
        $part | Format-Volume -FileSystem NTFS -NewFileSystemLabel 'WLVHDLAB' -Confirm:$false | Out-Null
        $part = $part | Get-Partition
        if ($part.DriveLetter -and [string]$part.DriveLetter -match '[A-Z]') {
            Say ("removing drive letter " + $part.DriveLetter)
            $part | Remove-PartitionAccessPath -AccessPath ([string]$part.DriveLetter + ':\')
        }
        New-Item -ItemType Directory -Force $apply | Out-Null
        $part | Add-PartitionAccessPath -AccessPath ($apply + '\')
        Check 'volume on build\lab\vhd\apply' (Test-Path $apply)
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $dismOut = @(Native { & dism.exe /English /Apply-Image "/ImageFile:$sourceWim" "/Index:$sourceIndex" "/ApplyDir:$apply" })
        $dismCode = $LASTEXITCODE
        $dismOut | Where-Object { $_.Trim() -and $_ -notmatch '\[\s*[=\s]*\d' } | ForEach-Object { Say ("    dism: " + $_) }
        Check ("dism /Apply-Image index $sourceIndex (exit $dismCode, " + [int]$watch.Elapsed.TotalSeconds + " s)") ($dismCode -eq 0)
        Check 'Windows\System32\config\SYSTEM applied' (Test-Path (Join-Path $apply 'Windows\System32\config\SYSTEM'))
        $part | Remove-PartitionAccessPath -AccessPath ($apply + '\')
        Dismount-DiskImage -ImagePath $target | Out-Null
        if ((Test-Path $apply) -and -not (IsReparse $apply)) { Remove-Item $apply -Force }
        Check 'detached again' (-not (Attached $target))
        Say ("{0}: {1:N0} bytes" -f $target, (Get-Item $target).Length)
    }
    if ($Phase -eq 'Run') {
        if (-not (Test-Path $Golden)) { throw 'golden.vhdx missing: run -Phase Prepare first' }
        if (Test-Path $Records) { throw "$Records exists: an earlier run left a record" }
        if ((Test-Path $Mnt) -and ((IsReparse $Mnt) -or (@(Get-ChildItem $Mnt -Force).Count -gt 0))) { throw "$Mnt is not empty" }
        if (Test-Path $ChangesDir) {
            if (@(Get-ChildItem $ChangesDir -Force).Count -gt 0) { throw "$ChangesDir is not empty" }
        }
        $changes = Join-Path $Lab 'changeset.json'
        $watch = [Diagnostics.Stopwatch]::StartNew()
        Copy-Item $Golden $Work -Force
        Say ("work copy made in " + [int]$watch.Elapsed.TotalSeconds + " s")
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $h0 = (Get-FileHash $Work -Algorithm SHA256).Hash
        Say ("work.vhdx sha256 before: $h0 (" + [int]$watch.Elapsed.TotalSeconds + " s)")
        $mountArgs = @('mount', "`"$Work`"", '1', "`"$Mnt`"", "--changes-dir=`"$ChangesDir`"", '--verbose')
        $child = Join-Path $ChangesDir 'work.winlove-changes.vhdx'

        # ---- 1. info: read only, attached for a moment ----
        Check 'info' ((Invoke-Wl 'info' @('info', "`"$Work`"")) -eq 0)
        Check 'info --json' ((Invoke-Wl 'info-json' @('info', "`"$Work`"", '--json')) -eq 0)
        $info = (OutText 'info-json') | ConvertFrom-Json
        $img = $info.install.images[0]
        Say ("info: " + $img.name + " | " + $img.editionId + " | " + $img.version + " | " + $img.architecture + " | " + ($img.languages -join ',') + " | used " + $img.totalBytes)
        Check 'info: Windows 11 Pro' ($img.name -eq 'Windows 11 Pro')
        Check 'info: build 26200, x64, tr-TR' (($img.build -eq 26200) -and ($img.architecture -eq 'x64') -and (@($img.languages) -contains 'tr-TR'))
        Check 'info: one edition, format VHDX' (($info.install.images.Count -eq 1) -and ($info.format -eq 'VHDX'))
        Check 'info leaves the disk detached' (-not (Attached $Work))

        # ---- 2. mount + read commands + apply, then discard ----
        Check 'mount' ((Invoke-Wl 'mount1' $mountArgs) -eq 0)
        Check 'mount: the changes disk is attached' (Attached $child)
        Check 'mount: the folder is a mount point with Windows in it' ((IsReparse $Mnt) -and (Test-Path (Join-Path $Mnt 'Windows\System32\config\SOFTWARE')))
        Check 'mount: no drive letter on the volume' (@(Get-Volume | Where-Object { $_.FileSystemLabel -eq 'WLVHDLAB' -and $_.DriveLetter }).Count -eq 0)
        Say ("work.vhdx attached itself (as the parent): " + (Attached $Work))
        [void](Invoke-Wl 'mounts1' @('mounts'))
        [void](Invoke-Wl 'mounts1-json' @('mounts', '--json'))
        Check 'mounts lists our disk as ok' ((OutText 'mounts1') -match 'virtual disk' -and (OutText 'mounts1') -match 'state: ok')
        foreach ($read in @(@('features', '--json'), @('packages'), @('capabilities', '--json'), @('appx', '--json'), @('drivers', '--json'), @('services'), @('optional-features', '--json'))) {
            $name = 'read-' + $read[0]
            $code = Invoke-Wl $name (@($read[0], "`"$Mnt`"") + @($read | Select-Object -Skip 1))
            $lines = @((OutText $name) -split "`n" | Where-Object { $_.Trim() }).Count
            # An applied image has no third-party driver: an empty list is the right answer there.
            $least = if ($read[0] -eq 'drivers') { 1 } else { 4 }
            Check ("read " + $read[0] + " ($lines lines, " + $script:lastSeconds + " s)") (($code -eq 0) -and ($lines -ge $least))
        }
        $marker0 = ImageValue 'SOFTWARE' 'WinLoveLab' 'VhdMarker'
        Check "marker absent before apply ($marker0)" ($marker0 -eq '<absent>')
        $code = Invoke-Wl 'apply1' @('apply', "`"$changes`"", "`"$Mnt`"", '--verbose')
        Check ("apply without commit (exit $code, " + $script:lastSeconds + " s)") ($code -eq 0)
        $marker1 = ImageValue 'SOFTWARE' 'WinLoveLab' 'VhdMarker'
        Check "marker written by apply ($marker1)" ($marker1 -match 'd141')
        Check 'OneDriveSetup.exe removed in the mounted image' (-not (Test-Path (Join-Path $Mnt 'Windows\System32\OneDriveSetup.exe')))
        Say ("changes disk now {0:N0} bytes" -f (Get-Item $child).Length)
        Check 'unmount --discard' ((Invoke-Wl 'discard1' @('unmount', "`"$Mnt`"", '--discard', '--verbose')) -eq 0)
        Check 'discard: changes disk deleted' (-not (Test-Path $child))
        Check 'discard: record gone' (-not (Test-Path $Records))
        Check 'discard: folder gone' (-not (Test-Path $Mnt))
        Check 'discard: nothing attached' ((-not (Attached $Work)) -and @(OurFiles | Where-Object { Attached $_ }).Count -eq 0)

        # ---- 3. mount again: nothing of the discarded run is there; then a crash in apply ----
        Check 'mount 2' ((Invoke-Wl 'mount2' $mountArgs) -eq 0)
        $marker2 = ImageValue 'SOFTWARE' 'WinLoveLab' 'VhdMarker'
        Check "after discard: marker absent ($marker2)" ($marker2 -eq '<absent>')
        Check 'after discard: OneDriveSetup.exe still there' (Test-Path (Join-Path $Mnt 'Windows\System32\OneDriveSetup.exe'))
        $hivesBefore = HiveList
        $o = Join-Path $Out 'apply-killed.out.txt'
        $e = Join-Path $Out 'apply-killed.err.txt'
        $p = Start-Process -FilePath $Cli -ArgumentList ("apply `"$changes`" `"$Mnt`" --verbose --vhd-records=`"$Records`"") -RedirectStandardOutput $o -RedirectStandardError $e -NoNewWindow -PassThru
        $null = $p.Handle
        $killedAt = ''
        for ($i = 0; $i -lt 240; $i++) {
            Start-Sleep -Milliseconds 500
            $text = Get-Content $o -Raw -ErrorAction SilentlyContinue
            if ($text -match '\[4/') { $killedAt = 'step 4'; break }
            if ($p.HasExited) { break }
        }
        if (-not $p.HasExited) {
            Stop-Process -Id $p.Id -Force
            $p.WaitForExit()
            Say ("apply killed at $killedAt (pid " + $p.Id + ")")
        } else {
            Say ("apply ended by itself before the kill (exit " + $p.ExitCode + ")")
        }
        Get-Process -Name DismHost -ErrorAction SilentlyContinue | ForEach-Object { Say ("DismHost still running: pid " + $_.Id) }
        Start-Sleep -Seconds 3
        $hivesAfterKill = HiveList
        $leftHives = @($hivesAfterKill | Where-Object { $hivesBefore -notcontains $_ })
        Say ("hives the killed process left loaded: " + $leftHives.Count)
        $leftHives | ForEach-Object { Say ("    " + $_) }
        [void](Invoke-Wl 'mounts-after-kill' @('mounts'))
        Check 'after the kill: a fresh wlcli finds its own disk by the record (state ok)' ((OutText 'mounts-after-kill') -match 'state: ok')
        Check 'unmount --discard after the kill' ((Invoke-Wl 'discard-after-kill' @('unmount', "`"$Mnt`"", '--discard', '--verbose')) -eq 0)
        $hivesEnd = HiveList
        Check 'no hive of the image left loaded' (@($hivesEnd | Where-Object { $hivesBefore -notcontains $_ }).Count -eq 0)
        Check 'after the kill: nothing attached, no record, no folder' ((@(OurFiles | Where-Object { Attached $_ }).Count -eq 0) -and -not (Test-Path $Records) -and -not (Test-Path $Mnt))

        # ---- 4. a reboot detaches every virtual disk: the record remounts it ----
        Check 'mount 3' ((Invoke-Wl 'mount3' $mountArgs) -eq 0)
        Say 'simulated reboot: our changes disk detached by its own path'
        Dismount-DiskImage -ImagePath $child | Out-Null
        [void](Invoke-Wl 'mounts-not-attached' @('mounts'))
        Check 'mounts says not attached / remount' ((OutText 'mounts-not-attached') -match 'not attached' -and (OutText 'mounts-not-attached') -match 'remount')
        Check 'repair (remount)' ((Invoke-Wl 'repair' @('repair', "`"$Mnt`"", '--verbose')) -eq 0)
        [void](Invoke-Wl 'mounts-repaired' @('mounts'))
        Check 'after repair: state ok' ((OutText 'mounts-repaired') -match 'state: ok')
        Check 'after repair: features read' ((Invoke-Wl 'read-after-repair' @('features', "`"$Mnt`"")) -eq 0)
        Check 'unmount --discard after repair' ((Invoke-Wl 'discard3' @('unmount', "`"$Mnt`"", '--discard')) -eq 0)
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $h1 = (Get-FileHash $Work -Algorithm SHA256).Hash
        Say ("work.vhdx sha256 after three discarded mounts, a kill and a reboot: $h1 (" + [int]$watch.Elapsed.TotalSeconds + " s)")
        Check 'the disk itself was never written (same SHA-256)' ($h1 -eq $h0)

        # ---- 5. apply --commit: saved by merging into the disk ----
        Check 'mount 4' ((Invoke-Wl 'mount4' $mountArgs) -eq 0)
        $code = Invoke-Wl 'apply-commit' @('apply', "`"$changes`"", "`"$Mnt`"", '--commit', '--verbose')
        Check ("apply --commit (exit $code, " + $script:lastSeconds + " s)") ($code -eq 0)
        Check 'commit: changes disk merged and deleted' (-not (Test-Path $child))
        Check 'commit: record gone, folder gone, nothing attached' ((-not (Test-Path $Records)) -and (-not (Test-Path $Mnt)) -and @(OurFiles | Where-Object { Attached $_ }).Count -eq 0)
        $h2 = (Get-FileHash $Work -Algorithm SHA256).Hash
        Check 'commit: the disk changed' ($h2 -ne $h0)
        Say ("work.vhdx after the merge: {0:N0} bytes" -f (Get-Item $Work).Length)
        Check 'info after the merge' ((Invoke-Wl 'info-after' @('info', "`"$Work`"")) -eq 0)

        Check 'mount 5 (verify)' ((Invoke-Wl 'mount5' $mountArgs) -eq 0)
        $marker5 = ImageValue 'SOFTWARE' 'WinLoveLab' 'VhdMarker'
        Check "saved: marker ($marker5)" ($marker5 -match 'd141')
        $tele = ImageValue 'SOFTWARE' 'Policies\Microsoft\Windows\DataCollection' 'AllowTelemetry'
        Check "saved: AllowTelemetry ($tele)" ($tele -match '0x0')
        $diag = ImageValue 'SYSTEM' 'ControlSet001\Services\DiagTrack' 'Start'
        Check "saved: DiagTrack start ($diag)" ($diag -match '0x4')
        Check 'saved: OneDriveSetup.exe gone' (-not (Test-Path (Join-Path $Mnt 'Windows\System32\OneDriveSetup.exe')))
        Say ('saved: System32 charmap.exe still there: ' + (Test-Path (Join-Path $Mnt 'Windows\System32\charmap.exe')))
        [void](Invoke-Wl 'verify-appx' @('appx', "`"$Mnt`"", '--json'))
        $appx = OutText 'verify-appx'
        Check 'saved: BingWeather and GetHelp no longer provisioned' (($appx -notmatch 'Microsoft.BingWeather') -and ($appx -notmatch 'Microsoft.GetHelp'))
        Check 'saved: other apps still provisioned' ($appx -match 'Microsoft.WindowsCalculator')
        [void](Invoke-Wl 'verify-features' @('features', "`"$Mnt`"", '--json'))
        $features = (OutText 'verify-features') | ConvertFrom-Json
        $wf = @($features | Where-Object { $_.name -eq 'WorkFolders-Client' })
        Check ("saved: WorkFolders-Client " + ($wf | ForEach-Object { $_.state })) (($wf.Count -eq 1) -and ($wf[0].state -ne 'Installed'))
        [void](Invoke-Wl 'verify-cbs' @('cbs', "`"$Mnt`"", 'OneDrive'))
        Say ("cbs OneDrive: " + ((OutText 'verify-cbs') -replace "`r?`n", ' | '))
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $scan = @(Native { & dism.exe /English "/Image:$Mnt" /Cleanup-Image /ScanHealth })
        $scanCode = $LASTEXITCODE
        $scan | Set-Content -Path (Join-Path $Out 'scanhealth.txt') -Encoding UTF8
        $line = "$($scan | Where-Object { $_ -match 'component store|corruption|repairable' } | Select-Object -Last 1)".Trim()
        Say ("SCANHEALTH exit $scanCode, " + [int]$watch.Elapsed.TotalSeconds + " s: " + $line)
        Check 'ScanHealth: no component store corruption' (($scanCode -eq 0) -and ($line -match 'No component store corruption'))
        Check 'unmount --discard (verification mount)' ((Invoke-Wl 'discard5' @('unmount', "`"$Mnt`"", '--discard')) -eq 0)
    }
    if ($Phase -eq 'VhdFormat') {
        $peChanges = Join-Path $Lab 'changeset-pe.json'
        $peChild = Join-Path $ChangesDir 'pe.winlove-changes.vhd'
        $peMount = @('mount', "`"$PeVhd`"", '1', "`"$Mnt`"", "--changes-dir=`"$ChangesDir`"", '--verbose')
        Check 'info (VHD)' ((Invoke-Wl 'pe-info' @('info', "`"$PeVhd`"")) -eq 0)
        Check 'mount (VHD)' ((Invoke-Wl 'pe-mount1' $peMount) -eq 0)
        Check 'the changes disk is a .vhd' (Attached $peChild)
        Check 'features (VHD)' ((Invoke-Wl 'pe-features' @('features', "`"$Mnt`"")) -eq 0)
        Check 'apply --commit (VHD)' ((Invoke-Wl 'pe-apply' @('apply', "`"$peChanges`"", "`"$Mnt`"", '--commit', '--verbose')) -eq 0)
        Check 'merged and detached' ((-not (Test-Path $peChild)) -and -not (Test-Path $Records))
        Check 'mount again (VHD)' ((Invoke-Wl 'pe-mount2' $peMount) -eq 0)
        # WinPE: DISM holds SOFTWARE while its session is open (a registry-only plan cannot load it):
        # the proof here is a feature, read back by DISM after the merge.
        [void](Invoke-Wl 'pe-features-after' @('features', "`"$Mnt`"", '--json'))
        $peFeatures = (OutText 'pe-features-after') | ConvertFrom-Json
        $es = @($peFeatures | Where-Object { $_.name -eq 'WinPE-EnhancedStorage' })
        Check ("VHD: WinPE-EnhancedStorage saved as " + ($es | ForEach-Object { $_.state })) (($es.Count -ge 1) -and ($es[0].state -ne 'Installed'))
        Check 'unmount --discard (VHD)' ((Invoke-Wl 'pe-discard' @('unmount', "`"$Mnt`"", '--discard')) -eq 0)
    }
}
catch {
    Say ("ERROR " + $_.Exception.Message + " @ " + $_.InvocationInfo.ScriptLineNumber)
    $result = 7
}
finally {
    SafetyNet
    $after = Snapshot 'state-after'
    $same = (MountTable $before) -eq (MountTable $after)
    Check 'DISM mount table identical before and after' $same
    Check 'nothing of ours attached at the end' (@(OurFiles | Where-Object { Attached $_ }).Count -eq 0)
    Check 'no file-backed virtual disk left' (@(Get-Disk | Where-Object { $_.BusType -eq 'File Backed Virtual' }).Count -eq 0)
    if ($Phase -eq 'Run' -and (Test-Path $Work) -and -not (Attached $Work)) {
        Remove-Item $Work -Force
        Say 'work.vhdx deleted'
    }
    if ($Phase -eq 'VhdFormat' -and (Test-Path $PeVhd) -and -not (Attached $PeVhd)) {
        Remove-Item $PeVhd -Force
        Say 'pe.vhd deleted'
    }
    Say ("free on C: " + [math]::Round((Get-PSDrive C).Free / 1GB, 1) + " GB")
}
if ($result -eq 0 -and $script:failed -gt 0) { $result = 3 }
$verdict = if ($result -eq 0) { 'PASS' } else { "FAIL result=$result failed=$($script:failed)" }
Say ("=== done " + $verdict + " " + (Get-Date -Format s))
Set-Content -Path $done -Value $verdict -Encoding ASCII
exit $result
