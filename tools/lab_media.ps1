<#
  D-080 lab: the setup media brought up to date (elevated). Copies the lab's setup media into
  build\lab\work\media (one edition), adds the cumulative update to every image of boot.wim and
  keeps Setup's files (wlcli boot-patch --lcu --setup-files), builds an ISO with the Setup dynamic
  update and those files in place of the folder's (wlcli iso --setup-du --boot-files) and checks what
  is inside it. -ForVm also puts the same files into the media folder itself, for
  lab_vm.ps1 -SetupFolder build\lab\work\media -Edition 1.
  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe",
    [int] $Edition = 4,
    [string] $Lcu = '',
    [string] $SetupDu = '',
    [switch] $ForVm
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$Cli = [System.IO.Path]::GetFullPath($Cli)
$work = Join-Path $Lab 'work\media-refresh'
$media = Join-Path $Lab 'work\media'
$saved = Join-Path $work 'saved'
$bootMount = Join-Path $Lab 'mount\media-boot'
$iso = Join-Path $work 'refreshed.iso'
$log = Join-Path $Lab 'out\lab-media.log'
if (-not $Lcu) { $Lcu = @(Get-ChildItem (Join-Path $Lab 'updates') -Filter 'windows11.0-kb5129195-*.msu')[0].FullName }
if (-not $SetupDu) { $SetupDu = @(Get-ChildItem (Join-Path $Lab 'updates\du') -Filter 'windows11.0-kb5127216-*.cab')[0].FullName }
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
Remove-Item $log -ErrorAction SilentlyContinue
$failed = 0
function Say([string] $text) { Add-Content -Path $log -Value $text -Encoding UTF8; Write-Host $text }
function Check([string] $what, [bool] $ok) { if ($ok) { Say ('PASS  ' + $what) } else { Say ('FAIL  ' + $what); $script:failed++ } }
function Run([string[]] $arguments) {
    Say ('> wlcli ' + ($arguments -join ' '))
    $output = & $Cli @arguments 2>&1 | ForEach-Object { "$_" }
    $script:lastExit = $LASTEXITCODE
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export|boot|iso)\s+)?\d+%\s*$' } | Select-Object -Last 6 | ForEach-Object { Say ('    ' + $_.TrimEnd()) }
    $script:lastOutput = $output -join "`n"
}
function Sha([string] $file) { (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash }

Say ('=== lab_media ' + (Get-Date -Format s))
Say ('LCU:      ' + $Lcu)
Say ('Setup DU: ' + $SetupDu)
foreach ($dir in $work, $media, $bootMount) { if (Test-Path $dir) { Remove-Item $dir -Recurse -Force } }
New-Item -ItemType Directory -Force $work, $media, $bootMount, $saved | Out-Null

# 1. A media folder of our own: the lab's setup files, one edition.
& robocopy.exe (Join-Path $Lab 'setup') $media /E /XF install.wim /NFL /NDL /NJH /NJS /NP | Out-Null
Get-ChildItem $media -Recurse -File | Where-Object { $_.IsReadOnly } | ForEach-Object { $_.IsReadOnly = $false }
Run @('export', (Join-Path $Lab 'setup\sources\install.wim'), "$Edition", (Join-Path $media 'sources\install.wim'))
Check 'media folder with one edition' ($script:lastExit -eq 0)
$bootBefore = (Get-Item (Join-Path $media 'sources\boot.wim')).Length
$setupBefore = Sha (Join-Path $media 'sources\setup.exe')

# 2. boot.wim: the cumulative update into every image, Setup's files kept.
$started = Get-Date
Run @('boot-patch', (Join-Path $media 'sources\boot.wim'), $bootMount, "--lcu=$Lcu", "--setup-files=$saved")
$seconds = [int]((Get-Date) - $started).TotalSeconds
Check "boot-patch with the cumulative update ($seconds s)" ($script:lastExit -eq 0)
if ($script:lastOutput -match 'cumulative update in (\d+) image\(s\); Setup image (\S+) -> (\S+)') {
    Check ('every boot image updated (' + $Matches[1] + '), Setup image ' + $Matches[2] + ' -> ' + $Matches[3]) (([int]$Matches[1] -ge 2) -and ([version]$Matches[3] -gt [version]$Matches[2]))
} else {
    Check 'boot-patch reported the update' $false
}
foreach ($name in 'sources\setup.exe', 'sources\setuphost.exe', 'sources\setupplatform.dll', 'bootmgfw.efi', 'bootmgr.efi') { Check "kept $name" (Test-Path (Join-Path $saved $name)) }
Say ('boot.wim ' + $bootBefore + ' -> ' + (Get-Item (Join-Path $media 'sources\boot.wim')).Length + ' bytes')

# 3. The ISO with the Setup dynamic update and Setup's files in place of the folder's.
Run @('iso', $media, $iso, '--label=WL_MEDIA', '--boot=uefi', '--no-prompt', "--setup-du=$SetupDu", "--boot-files=$saved")
Check 'ISO built with the media files replaced' ($script:lastExit -eq 0 -and $script:lastOutput -match 'replaced')
$sevenZip = 'C:\Program Files\7-Zip\7z.exe'
if (Test-Path $sevenZip) {
    $check = Join-Path $work 'fromiso'
    & $sevenZip x $iso 'sources\setup.exe' 'sources\en-US' 'efi\boot\bootx64.efi' -o"$check" -y | Out-Null
    Check 'a folder the media did not have came in (sources\en-US)' (Test-Path (Join-Path $check 'sources\en-US'))
    Check 'the ISO has the updated Setup (setup.exe = boot.wim''s)' ((Sha (Join-Path $check 'sources\setup.exe')) -eq (Sha (Join-Path $saved 'sources\setup.exe')))
    Check 'the ISO has the updated boot manager (bootx64.efi = bootmgfw.efi)' ((Sha (Join-Path $check 'efi\boot\bootx64.efi')) -eq (Sha (Join-Path $saved 'bootmgfw.efi')))
    Check 'setup.exe changed against the original media' ($setupBefore -ne (Sha (Join-Path $saved 'sources\setup.exe')))
}

# 4. For a VM: the same files in the folder itself (lab_vm builds its own ISO from it).
if ($ForVm) {
    $du = Join-Path $work 'du'
    New-Item -ItemType Directory -Force $du | Out-Null
    & expand.exe $SetupDu -F:* $du | Out-Null
    Copy-Item (Join-Path $du '*') (Join-Path $media 'sources') -Recurse -Force
    Copy-Item (Join-Path $saved 'sources\*') (Join-Path $media 'sources') -Recurse -Force
    Get-ChildItem $media -Recurse -File -Include bootmgfw.efi, bootx64.efi | ForEach-Object { Copy-Item (Join-Path $saved 'bootmgfw.efi') $_.FullName -Force }
    Get-ChildItem $media -Recurse -File -Include bootmgr.efi | ForEach-Object { Copy-Item (Join-Path $saved 'bootmgr.efi') $_.FullName -Force }
    if (Test-Path (Join-Path $saved 'boot.stl')) { Copy-Item (Join-Path $saved 'boot.stl') (Join-Path $media 'efi\microsoft\boot\boot.stl') -Force }
    Say ('media folder refreshed in place for a VM: ' + $media)
}
Remove-Item $iso -ErrorAction SilentlyContinue
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
exit $failed
