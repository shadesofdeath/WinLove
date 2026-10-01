<#
.SYNOPSIS
  Engine check for D-056 (Kisisellestirme) on a real image: default wallpaper / lock screen /
  account picture / OEM logo replaced, a font added, OEM information written, a Wi-Fi profile
  queued for after setup, WinRE removed (D-031 recipe). Checks that every rewritten picture has
  the size the original had, that it is a new file (one link) while its WinSxS twin is unchanged,
  and that DISM still finds the component store healthy (/ScanHealth).
  Works on a one-edition export (Pro) of the lab WIM in build\lab\work\branding, mounted into
  build\lab\mount\branding; everything is discarded and deleted at the end. The ISO is only read.

  Needs an elevated PowerShell (about 10 minutes, 15 GB free) and build\lab\setup\sources\install.wim:
    powershell -ExecutionPolicy Bypass -File tools\lab_branding.ps1 [-SkipScanHealth]
  The whole output is also written to build\lab\out\branding-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [switch] $SkipScanHealth,
    [int] $Index = 4,
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
if (-not (Test-Path $source)) { $source = Join-Path $Lab 'iso\sources\install.wim' }
$work = Join-Path $Lab 'work\branding'
$wim = Join-Path $work 'branding.wim'
$mount = Join-Path $Lab 'mount\branding'
$log = Join-Path $Lab 'out\branding-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $source)) { throw "No lab install.wim: run  wlcli extract-all <test.iso> build\lab\setup  first" }
foreach ($dir in 'out', 'mount') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work, $mount | Out-Null

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
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export)\s+)?\d+%\s*$' } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*(mount|commit|discard|export)\s+\d+%)+', '').TrimEnd()) }
    Say ("  (exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $output
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}
function WriteUtf8([string] $path, [string] $text) {
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding $false))
}
function Hash([string] $path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash }
function Size([string] $path) {
    $image = [System.Drawing.Image]::FromFile($path)
    try { return "$($image.Width)x$($image.Height)" } finally { $image.Dispose() }
}
function Links([string] $path) { @(Native { fsutil hardlink list $path } | Where-Object { $_.Trim() }).Count }
function RegString([string] $text) { '"' + ($text -replace '\\', '\\' -replace '"', '\"') + '"' }

Say ("=== lab_branding " + (Get-Date -Format s))
try {
    Run @('export', $source, "$Index", $wim) | Out-Null
    Check "export edition $Index" ($script:lastExit -eq 0)
    Run @('mount', $wim, '1', $mount) | Out-Null
    Check 'mount read-write' ($script:lastExit -eq 0)

    # The pictures as they are now, and their WinSxS twins.
    $targets = @()
    $targets += Get-ChildItem (Join-Path $mount 'Windows\Web\Wallpaper\Windows') -Filter *.jpg
    $targets += Get-ChildItem (Join-Path $mount 'Windows\Web\4K\Wallpaper\Windows') -Filter *.jpg
    $targets += Get-Item (Join-Path $mount 'Windows\Web\Screen\img100.jpg')
    $targets += Get-ChildItem (Join-Path $mount 'ProgramData\Microsoft\User Account Pictures') |
        Where-Object { $_.Name -like 'user*' -and ($_.Extension -eq '.png' -or $_.Extension -eq '.bmp') }
    $before = @{}
    foreach ($t in $targets) { $before[$t.FullName] = @{ hash = (Hash $t.FullName); size = (Size $t.FullName); links = (Links $t.FullName) } }
    Say ("INFO  " + $targets.Count + " default pictures; links: " + (($targets | ForEach-Object { $before[$_.FullName].links }) -join ','))
    $twin = Get-ChildItem (Join-Path $mount 'Windows\WinSxS') -Directory -Filter '*l-wallpaper-windows_*' |
        ForEach-Object { Join-Path $_.FullName 'img0.jpg' } | Where-Object { Test-Path $_ } | Select-Object -First 1
    $twinHash = if ($twin) { Hash $twin } else { '' }
    Check "WinSxS twin of img0.jpg found ($twin)" ([bool]$twin)

    # Inputs: a picture of our own, a font the image does not have, a Wi-Fi profile.
    $picture = Join-Path $work 'lab-picture.png'
    $bmp = New-Object System.Drawing.Bitmap 1600, 1000
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Point 0, 0), (New-Object System.Drawing.Point 1600, 1000), ([System.Drawing.Color]::DarkOrange), ([System.Drawing.Color]::MidnightBlue)
    $g.FillRectangle($brush, 0, 0, 1600, 1000)
    $g.Dispose(); $bmp.Save($picture, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $imageFonts = @{}
    Get-ChildItem (Join-Path $mount 'Windows\Fonts') | ForEach-Object { $imageFonts[$_.Name.ToLower()] = $true }
    $font = Get-ChildItem C:\Windows\Fonts -Filter *.ttf | Where-Object { -not $imageFonts.ContainsKey($_.Name.ToLower()) } |
        Sort-Object Length | Select-Object -First 1
    if (-not $font) { $font = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\Windows\Fonts" -Filter *.ttf -ErrorAction SilentlyContinue | Select-Object -First 1 }
    $fontName = ''
    if ($font) {
        $fontName = (Native { & $Cli font-info $font.FullName } | Select-Object -First 1).Trim()
        Say "INFO  font not in the image: $($font.Name) = $fontName"
    }
    $wifi = (Native { & $Cli wifi-xml --ssid=WinLoveLab --password=labpass123 }) -join "`r`n"

    $oem = 'HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\OEMInformation'
    $ops = @(
        @{ kind = 'setPicture'; target = 'wallpaper'; value = $picture; risk = 'low' },
        @{ kind = 'setPicture'; target = 'lockscreen'; value = $picture; risk = 'low' },
        @{ kind = 'setPicture'; target = 'account'; value = $picture; risk = 'low' },
        @{ kind = 'setPicture'; target = 'oemlogo'; value = $picture; risk = 'low' },
        @{ kind = 'setRegistryValue'; target = "$oem::Manufacturer"; value = (RegString 'WinLove Lab'); risk = 'low' },
        @{ kind = 'setRegistryValue'; target = "$oem::SupportURL"; value = (RegString 'https://example.com/support'); risk = 'low' },
        @{ kind = 'removeComponent'; target = 'winre'; value = '{"title":"WinRE","paths":["Windows\\System32\\Recovery\\Winre.wim"]}'; risk = 'high' },
        @{ kind = 'setPostSetup'; target = 'postsetup'; risk = 'low';
           value = (@{ when = 'firstLogon'; continueOnError = $true; steps = @(@{ type = 'wifi'; name = 'WinLoveLab'; source = $wifi }) } | ConvertTo-Json -Depth 5 -Compress) }
    )
    if ($font) { $ops += @{ kind = 'addFont'; target = $font.Name; value = $font.FullName; risk = 'low' } }
    $cs = Join-Path $work 'queue.json'
    WriteUtf8 $cs (([ordered]@{ format = 'winlove.changeset'; version = 1; operations = $ops }) | ConvertTo-Json -Depth 6)
    Check 'WinRE is in the image before' (Test-Path (Join-Path $mount 'Windows\System32\Recovery\Winre.wim'))
    Run @('apply', $cs, $mount) | Out-Null
    Check "queue applied ($($ops.Count) operations)" ($script:lastExit -eq 0)

    # Pictures: same size as before, new content, one link each; WinSxS twin unchanged.
    foreach ($t in $targets) {
        $p = $t.FullName
        $ok = (Test-Path $p) -and ((Hash $p) -ne $before[$p].hash) -and ((Size $p) -eq $before[$p].size) -and ((Links $p) -eq 1)
        Check ("picture " + $p.Substring($mount.Length + 1) + " " + $before[$p].size + " rewritten, own file") $ok
    }
    if ($twin) { Check 'WinSxS twin of img0.jpg unchanged' ((Hash $twin) -eq $twinHash) }
    $logo = Join-Path $mount 'Windows\System32\oemlogo.bmp'
    Check 'oemlogo.bmp 120x120' ((Test-Path $logo) -and ((Size $logo) -eq '120x120'))
    Check 'Winre.wim removed' (-not (Test-Path (Join-Path $mount 'Windows\System32\Recovery\Winre.wim')))
    if ($font) { Check "font file in Windows\Fonts\$($font.Name)" ((Hash (Join-Path $mount "Windows\Fonts\$($font.Name)")) -eq (Hash $font.FullName)) }

    # Registry: what Applier wrote, read back from the offline hives.
    $reg = @('Windows Registry Editor Version 5.00', '',
        "[$($oem -replace '^HKLM','HKEY_LOCAL_MACHINE')]",
        ('"Manufacturer"=' + (RegString 'WinLove Lab')),
        ('"SupportURL"=' + (RegString 'https://example.com/support')),
        ('"Logo"=' + (RegString 'C:\Windows\System32\oemlogo.bmp')), '',
        '[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\PersonalizationCSP]',
        ('"LockScreenImagePath"=' + (RegString 'C:\Windows\Web\Screen\img100.jpg')),
        '"LockScreenImageStatus"=dword:00000001')
    if ($font) {
        $reg += @('', '[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts]', ((RegString $fontName) + '=' + (RegString $font.Name)))
    }
    $regFile = Join-Path $work 'expected.reg'
    [System.IO.File]::WriteAllText($regFile, ($reg -join "`r`n") + "`r`n", [System.Text.Encoding]::Unicode)
    $text = (Native { & $Cli reg-check $regFile $mount --json }) -join "`n"
    $rows = @($text | ConvertFrom-Json | ForEach-Object { $_ })
    foreach ($r in $rows) { Check ("registry " + $r.target + " " + $r.status) ($r.status -eq 'in-image') }

    # Wi-Fi: profile staged for netsh, script calls it and deletes it.
    $wl = Join-Path $mount 'Windows\Setup\Scripts\WinLove'
    Check 'wifi.xml staged' (Test-Path (Join-Path $wl 'files\1\wifi.xml'))
    $machine = Get-Content (Join-Path $wl 'postsetup-machine.cmd') -Raw
    Check 'machine script adds the profile for all users' ($machine -like '*netsh wlan add profile filename=*wifi.xml* user=all*')
    Check 'machine script deletes the profile file' ($machine -like '*del /f /q*wifi.xml*')
    Check 'SetupComplete.cmd calls the post-setup script' ((Get-Content (Join-Path $mount 'Windows\Setup\Scripts\SetupComplete.cmd') -Raw) -like '*postsetup-machine.cmd*')

    if (-not $SkipScanHealth) {
        Say "`n> dism /Image /Cleanup-Image /ScanHealth"
        $w = [Diagnostics.Stopwatch]::StartNew()
        $scan = Native { dism.exe /English /Image:$mount /Cleanup-Image /ScanHealth }
        $scan | Where-Object { $_ -match 'repairable|No component store corruption|corrupt|Error' } | ForEach-Object { Say "  $_" }
        Check ("component store healthy (" + [int]$w.Elapsed.TotalSeconds + " s)") ([bool]($scan -match 'No component store corruption detected'))
    }
} finally {
    if (Test-Path (Join-Path $mount 'Windows')) { Native { & $Cli unmount $mount --discard } | Out-Null }
    Native { & $Cli cleanup } | Out-Null
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    Check 'lab WIM and work files deleted' (-not (Test-Path $wim))
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
