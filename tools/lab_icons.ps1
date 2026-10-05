<#
.SYNOPSIS
  Engine check for D-068 (icons patched inside Windows' own files) on a real image:
    - wlcli icon-image patches imageres.dll.mun in a mounted copy of edition 4;
    - the file keeps its owner (TrustedInstaller) and DACL, its WinSxS twin keeps the original
      bytes (the hard link is broken, not written through), the backup is the original, the
      restore script lists it, Windows loads every icon of the patched file;
    - patching again builds on the file (earlier icons stay), the backup stays Microsoft's;
      --restore puts the original back;
    - DISM /ScanHealth finds the component store healthy;
    - with -Lcu: the cumulative update from build\lab\updates installs afterwards (DISM), and
      the store is still healthy (the update may or may not replace the patched file: reported).
  Works on a copy in build\lab\work\icons, mounted into build\lab\mount\icons; discarded at the end.

    powershell -ExecutionPolicy Bypass -File tools\lab_icons.ps1 [-Lcu]      (elevated; ~5 min, -Lcu ~40 min)

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe",
    [int] $Edition = 4,
    [switch] $Lcu
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
$work = Join-Path $Lab 'work\icons'
$mount = Join-Path $Lab 'mount\icons'
$log = Join-Path $Lab 'out\icons-test.log'
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $source)) { throw "no $source (run tools\lab_setup.ps1 first)" }
foreach ($dir in 'out', 'mount') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
if (Test-Path (Join-Path $mount 'Windows')) { & $Cli unmount $mount --discard | Out-Null }
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work, $mount | Out-Null
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
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export)\s+)?\d+%\s*$' } | ForEach-Object { Say ("  " + $_.TrimEnd()) }
    Say ("  (exit $script:lastExit)")
    return $output
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }
function Hash([string] $path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash }
function Sddl([string] $path) { (Get-Acl -LiteralPath $path).Sddl }
function Links([string] $path) { @(Native { fsutil.exe hardlink list $path } | Where-Object { $_.Trim() }) }
function ScanHealth() {
    Say "`n> dism /Image:$mount /Cleanup-Image /ScanHealth"
    $out = Native { dism.exe /English "/Image:$mount" /Cleanup-Image /ScanHealth }
    $out | Where-Object { $_ -match 'corrupt|repairable|error' } | ForEach-Object { Say ("  " + $_.Trim()) }
    return (($out -join "`n") -match 'No component store corruption detected')
}

Say ("=== lab_icons " + (Get-Date -Format s))
try {
    Run @('export', $source, "$Edition", "$work\image.wim") | Out-Null
    Run @('mount', "$work\image.wim", '1', $mount) | Out-Null
    Check "mount edition $Edition" ($script:lastExit -eq 0)

    $rel = 'Windows\SystemResources\imageres.dll.mun'
    $rel2 = 'Windows\SystemResources\shell32.dll.mun'
    $target = Join-Path $mount $rel
    $target2 = Join-Path $mount $rel2
    $backup = Join-Path $mount 'Windows\WinLove\IconBackup\SystemResources\imageres.dll.mun'
    $script = Join-Path $mount 'Windows\WinLove\IconBackup\restore-icons.cmd'
    $origHash = Hash $target
    $origHash2 = Hash $target2
    $origSddl = Sddl $target
    $links = Links $target
    Say ("imageres links before: " + ($links -join ' | '))
    $twin = $links | Where-Object { $_ -match 'WinSxS' } | Select-Object -First 1
    if ($twin) { $twin = Join-Path ([System.IO.Path]::GetPathRoot($mount)) $twin.TrimStart('\') }
    Check 'imageres.dll.mun is a hard link into WinSxS' ($twin -and (Test-Path -LiteralPath $twin))
    Check 'owner is TrustedInstaller' ($origSddl -match '^O:S-1-5-80-956008885')

    # Icons to put in: shell32's #3 and imageres' own #2 (both from the image).
    Run @('icon-extract', $target2, '3', "$work\a.ico") | Out-Null
    Run @('icon-extract', $target, '2', "$work\b.ico") | Out-Null
    Run @('icon-extract', $target, '4', "$work\orig4.ico") | Out-Null

    Run @('icon-image', $mount, $rel, "3=$work\a.ico", "4=$work\b.ico") | Out-Null
    Check 'icon-image patch' ($script:lastExit -eq 0)
    Check 'the file changed' ((Hash $target) -ne $origHash)
    Check 'backup = original' ((Test-Path -LiteralPath $backup) -and (Hash $backup) -eq $origHash)
    $newSddl = Sddl $target
    if ($newSddl -ne $origSddl) { Say "  before: $origSddl"; Say "  after:  $newSddl" }
    Check 'owner and DACL kept (SDDL identical)' ($newSddl -eq $origSddl)
    Check 'WinSxS twin still the original' ($twin -and (Hash $twin) -eq $origHash)
    Check 'patched file is one link (the WinSxS one is apart)' (@(Links $target).Count -eq 1)
    Check 'restore script lists it' ((Test-Path -LiteralPath $script) -and ((Get-Content -LiteralPath $script -Raw) -match 'imageres\.dll\.mun'))
    Run @('icon-verify', $target) | Out-Null
    Check 'Windows loads every icon of the patched file' ($script:lastExit -eq 0)
    Run @('icon-extract', $target, '3', "$work\after3.ico") | Out-Null
    Check 'group 3 is the new icon' ((Hash "$work\after3.ico") -eq (Hash "$work\a.ico"))

    # Again with only #3: #4 keeps the first patch (the base is the file as it is), the backup stays Microsoft's.
    Run @('icon-image', $mount, $rel, "3=$work\b.ico") | Out-Null
    Check 'second patch' ($script:lastExit -eq 0)
    Run @('icon-extract', $target, '4', "$work\again4.ico") | Out-Null
    Run @('icon-extract', $target, '3', "$work\again3.ico") | Out-Null
    Check 'group 4 keeps the first patch, group 3 the second' (((Hash "$work\again4.ico") -eq (Hash "$work\b.ico")) -and ((Hash "$work\again3.ico") -eq (Hash "$work\b.ico")))
    Check 'backup still the original' ((Hash $backup) -eq $origHash)

    # shell32: patch, then restore.
    Run @('icon-image', $mount, $rel2, "3=$work\b.ico") | Out-Null
    Check 'shell32 patch' ($script:lastExit -eq 0)
    Run @('icon-image', $mount, $rel2, '--restore') | Out-Null
    Check 'shell32 restore' ($script:lastExit -eq 0)
    Check 'shell32 is the original again' ((Hash $target2) -eq $origHash2)
    Check 'restore script no longer lists shell32' (-not ((Get-Content -LiteralPath $script -Raw) -match 'shell32'))

    # Refusals.
    Run @('icon-image', $mount, 'Windows\explorer.exe', "1=$work\a.ico") | Out-Null
    Check 'explorer.exe (program code) refused' ($script:lastExit -ne 0)
    Run @('icon-image', $mount, ($twin.Substring($mount.Length).TrimStart('\')), "3=$work\a.ico") | Out-Null
    Check 'the WinSxS copy refused' ($script:lastExit -ne 0)

    Check 'DISM ScanHealth: component store healthy' (ScanHealth)

    if ($Lcu) {
        $msu = Get-ChildItem (Join-Path $Lab 'updates') -Filter 'windows11.0-kb5129195*.msu' | Select-Object -First 1
        if (-not $msu) { throw 'no LCU in build\lab\updates' }
        $doc = [ordered]@{ format = 'winlove.changeset'; version = 1; operations = @([ordered]@{ kind = 'addPackage'; target = $msu.FullName; value = ''; risk = 'low' }) }
        [System.IO.File]::WriteAllText("$work\lcu.json", ($doc | ConvertTo-Json -Depth 5), (New-Object System.Text.UTF8Encoding $false))
        Run @('apply', "$work\lcu.json", $mount) | Out-Null
        Check "cumulative update $($msu.Name.Substring(0, 22)) installs after the patch" ($script:lastExit -eq 0)
        $stillPatched = (Hash $target) -ne $origHash -and -not ((Links $target) -match 'WinSxS')
        Say ("INFO  after the update imageres.dll.mun is " + $(if ($stillPatched) { 'still patched' } else { "Microsoft's (the update replaced it)" }))
        Check 'DISM ScanHealth after the update' (ScanHealth)
    }
} catch {
    Say ("ERROR  " + $_.Exception.Message + " (line " + $_.InvocationInfo.ScriptLineNumber + ")")
    $failed++
} finally {
    if (Test-Path (Join-Path $mount 'Windows')) { Run @('unmount', $mount, '--discard') | Out-Null }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
