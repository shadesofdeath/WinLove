<#
.SYNOPSIS
  D-113 engine check: the component definitions' store split and the removal of a component's
  installed copies (the reference tool's custom removal with update support kept), on a COPY of the lab
  install.wim. Splits the store with the shipped catalog, removes Character Map (every
  architecture), the 32-bit part of WMIC only, and WebClient with its service; checks that the
  files went from System32 / SysWOW64 while the WinSxS payload stayed, that a core component and a
  core service are refused, and that DISM still finds a healthy component store (/ScanHealth).
  Unmounts with DISCARD. Nothing outside build\lab is touched; the test ISO is never written.

  Needs an elevated PowerShell:
    powershell -ExecutionPolicy Bypass -File tools\lab_store_copies.ps1
  The whole output is also written to build\lab\out\store-copies-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [int] $Index = 4,              # Windows 11 Pro in the 25H2 lab image
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe",
    [string] $Catalog = "$PSScriptRoot\..\resources\catalog\components.json",
    [switch] $SkipScanHealth
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$golden = Join-Path $Lab 'setup\sources\install.wim'
$work = Join-Path $Lab 'work\store-copies.wim'
$mount = Join-Path $Lab 'mount\store-copies'
$out = Join-Path $Lab 'out'
$log = Join-Path $out 'store-copies-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $golden)) { throw "Lab image missing ($golden)" }
foreach ($dir in 'work', 'mount\store-copies', 'out') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }

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
    $output = Native { & $Cli @arguments }
    $code = $LASTEXITCODE
    $output | Where-Object { $_ -notmatch '^\s*((mount|discard|commit|remove)\s+)?[\d.]+%\s*$' -and $_.Trim() } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*[\d.]+%)+', '').TrimEnd()) }
    return $code
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}
function InHive([string] $hiveFile, [string] $key) {
    $name = 'WinLoveLabCheck'
    Native { reg.exe load "HKLM\$name" $hiveFile } | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "could not load $hiveFile" }
    Native { reg.exe query "HKLM\$name\$key" } | Out-Null
    $found = $LASTEXITCODE -eq 0
    [gc]::Collect()
    Native { reg.exe unload "HKLM\$name" } | Out-Null
    return $found
}
# A recipe file: these store names (bits 0 / 32 / 64) and services.
function Recipe([string] $title, [string[]] $names, [int] $bits, [string[]] $services) {
    $doc = [ordered]@{ title = $title; storeNames = @($names); afterUpdates = $true }
    if ($bits -ne 0) { $doc.storeBits = $bits }
    if ($services) { $doc.services = @($services) }
    $file = Join-Path $out ('store-' + ($title -replace '[^A-Za-z0-9]', '_') + '.json')
    [System.IO.File]::WriteAllText($file, ($doc | ConvertTo-Json -Compress), (New-Object System.Text.UTF8Encoding($false)))
    Say ("  recipe: " + ($doc | ConvertTo-Json -Compress))
    return $file
}
# A WinSxS payload folder of this architecture still holds this file.
function InStore([string] $arch, [string] $file) {
    $hit = Get-ChildItem (Join-Path $mount 'Windows\WinSxS') -Directory -Filter "$arch`_*" -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName $file) } | Select-Object -First 1
    return [bool] $hit
}

Remove-Item $log -ErrorAction SilentlyContinue
Say "WinLove store copies removal check (D-113) - $(Get-Date -Format s)"
Say "copying the lab image (the original stays untouched)..."
Copy-Item $golden $work -Force

$mounted = $false
try {
    $mounted = (Run @('mount', $work, "$Index", $mount)) -eq 0
    Check 'mount' $mounted
    if (-not $mounted) { throw 'the image could not be mounted: nothing else can be checked' }
    $system = Join-Path $mount 'Windows\System32\config\SYSTEM'
    $sys32 = Join-Path $mount 'Windows\System32'
    $wow = Join-Path $mount 'Windows\SysWOW64'

    # ---- the split ------------------------------------------------------------------------------
    $lines = Native { & $Cli store-share $mount $Catalog }
    Check 'store-share reads the store' ($LASTEXITCODE -eq 0)
    $share = @{}
    foreach ($line in $lines) {
        if ($line -match '^\{"bytes') { $o = $line | ConvertFrom-Json; $share[$o.id] = $o }
    }
    $total = 0
    foreach ($o in $share.Values) { $total += $o.bytes64 + $o.bytes32 }
    Say ("  " + $share.Count + " definitions own " + [math]::Round($total / 1MB) + " MB of the store")
    Check 'most definitions find their components' ($share.Count -gt 100)

    # ---- Character Map, every architecture ------------------------------------------------------
    $charmap = $share['charmap']
    Check 'charmap is in the store' ($null -ne $charmap -and @($charmap.names64).Count -gt 0)
    Check 'System32\charmap.exe is there' (Test-Path (Join-Path $sys32 'charmap.exe'))
    $names = @(@($charmap.names64) + @($charmap.names32) | Sort-Object -Unique)
    Check 'remove Character Map' ((Run @('component', $mount, (Recipe 'Karakter Esleme' $names 0 @()), '--remove')) -eq 0)
    Check 'System32\charmap.exe is gone' (-not (Test-Path (Join-Path $sys32 'charmap.exe')))
    Check 'SysWOW64\charmap.exe is gone' (-not (Test-Path (Join-Path $wow 'charmap.exe')))
    Check 'the WinSxS payload stays (update support)' (InStore 'amd64' 'charmap.exe')

    # ---- WMIC, the 32-bit part only -------------------------------------------------------------
    $wmic = $share['wmic']
    if ($wmic -and @($wmic.names32).Count -gt 0 -and (Test-Path (Join-Path $wow 'wbem\WMIC.exe'))) {
        Check 'remove WMIC - 32 bit' ((Run @('component', $mount, (Recipe 'WMIC 32' @($wmic.names32) 32 @()), '--remove')) -eq 0)
        Check 'SysWOW64\wbem\WMIC.exe is gone' (-not (Test-Path (Join-Path $wow 'wbem\WMIC.exe')))
        Check 'System32\wbem\WMIC.exe stays' (Test-Path (Join-Path $sys32 'wbem\WMIC.exe'))
    } else {
        Say '  (no 32-bit WMIC in this image)'
    }

    # ---- WebClient with its service ---------------------------------------------------------------
    $web = $share['webclient']
    if ($web -and (Test-Path (Join-Path $sys32 'webclnt.dll'))) {
        Check 'Services\WebClient is there' (InHive $system 'ControlSet001\Services\WebClient')
        $names = @(@($web.names64) + @($web.names32) | Sort-Object -Unique)
        Check 'remove WebClient' ((Run @('component', $mount, (Recipe 'WebClient' $names 0 @('WebClient')), '--remove')) -eq 0)
        Check 'System32\webclnt.dll is gone' (-not (Test-Path (Join-Path $sys32 'webclnt.dll')))
        Check 'Services\WebClient is gone' (-not (InHive $system 'ControlSet001\Services\WebClient'))
        Check 'Services\LanmanWorkstation stays' (InHive $system 'ControlSet001\Services\LanmanWorkstation')
    } else {
        Say '  (no WebClient in this image)'
    }

    # ---- what Windows cannot start without is refused -------------------------------------------
    Check 'a core component is refused' ((Run @('component', $mount, (Recipe 'Kernel' @('microsoft-windows-kernel32') 0 @()), '--remove')) -ne 0)
    Check 'kernel32.dll stays' (Test-Path (Join-Path $sys32 'kernel32.dll'))
    Check 'a core service is refused' ((Run @('component', $mount, (Recipe 'Rpc' @('microsoft-windows-charmap') 0 @('RpcSs')), '--remove')) -ne 0)

    # ---- the image is still serviceable, its store healthy -------------------------------------
    $packages = Native { & $Cli packages $mount }
    Say ("`n> wlcli packages: exit $LASTEXITCODE, " + @($packages).Count + " line(s)")
    Check 'DISM still lists packages' ($LASTEXITCODE -eq 0 -and @($packages).Count -gt 50)
    if (-not $SkipScanHealth) {
        Say "`n> dism /Image /Cleanup-Image /ScanHealth (minutes)"
        $scan = Native { dism.exe /English "/Image:$mount" /Cleanup-Image /ScanHealth }
        $scan | Where-Object { $_ -match 'component store|repairable|corruption|operation' } | ForEach-Object { Say "  $_" }
        Check 'ScanHealth: no component store corruption' ($LASTEXITCODE -eq 0 -and (($scan -join ' ') -match 'No component store corruption detected'))
    }
}
catch {
    Say "FAIL  script error: $($_.Exception.Message)"
    $failed++
}
finally {
    if ($mounted) {
        Check 'unmount (discard)' ((Run @('unmount', $mount, '--discard')) -eq 0)
    }
    Remove-Item $work -Force -ErrorAction SilentlyContinue
}
if ($failed) { $summary = "$failed check(s) FAILED" } else { $summary = 'all checks passed' }
Say "`n$summary - log: $log"
exit $failed
