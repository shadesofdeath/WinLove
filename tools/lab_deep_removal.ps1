<#
.SYNOPSIS
  Engine check for P07 deep removal (D-060): on a COPY of one edition of the lab install.wim, every
  catalog entry with driverClasses (resources\catalog\components.json) is removed with
  `wlcli component --remove`. Then: no driver of those classes is left (probe), no "dual_" payload
  in WinSxS, DISM /ScanHealth must stay clean; optionally a newer cumulative update is added
  (-Lcu <msu>) and the store is scanned again, and whether a driver came back is reported. Commit,
  fresh export, verify. Nothing outside build\lab is touched; the test ISO is never read.

    powershell -ExecutionPolicy Bypass -File tools\lab_deep_removal.ps1 [-Lcu <newer LCU .msu> [-LcuFirst]] [-Index 4]
  -LcuFirst adds the update BEFORE the deep removal (the order Apply uses: updates, then components).
  Needs an elevated PowerShell. About 10 minutes, 40-60 more with -Lcu.
  Log: build\lab\out\deep-removal-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [string] $Lcu = '',
    [switch] $LcuFirst,
    [int] $Index = 4,              # Windows 11 Pro in the 25H2 test ISO
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-release\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
if (-not (Test-Path $source)) { $source = Join-Path $Lab 'iso\sources\install.wim' }
$work = Join-Path $Lab 'work\deep'
$mount = Join-Path $Lab 'mount\deep'
$log = Join-Path $Lab 'out\deep-removal-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1 -Config Release" }
if (-not (Test-Path $source)) { throw "Lab image missing ($source)" }
if ($Lcu -and -not (Test-Path $Lcu)) { throw "No such update: $Lcu" }
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work, $mount, (Join-Path $Lab 'out'), (Join-Path $work 'recipes') | Out-Null

$failed = 0
function Say([string] $text) { Add-Content -Path $log -Value $text -Encoding UTF8; Write-Host $text }
function Native([scriptblock] $command) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $command 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = $previous
    return $output
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }
function Mb([string] $file) { [math]::Round((Get-Item $file).Length / 1MB) }
function ScanHealth([string] $when) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $scan = @(Native { & dism.exe /English "/Image:$mount" /Cleanup-Image /ScanHealth })
    $line = "$($scan | Where-Object { $_ -match 'component store|corruption|repairable' } | Select-Object -Last 1)".Trim()
    Check ("ScanHealth $when`: $line (" + [int]$watch.Elapsed.TotalSeconds + " s)") ($LASTEXITCODE -eq 0 -and $line -match 'No component store corruption')
}
function AddLcu() {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $add = @(Native { & dism.exe /English "/Image:$mount" /Add-Package "/PackagePath:$Lcu" })
    $code = $LASTEXITCODE
    $add | Where-Object { $_ -match 'error|completed|Processing' } | Select-Object -Last 3 | ForEach-Object { Say ("        " + $_.Trim()) }
    Check ("cumulative update added (exit $code, " + [int]$watch.Elapsed.TotalMinutes + " min)") ($code -eq 0)
}
function Probe([System.IO.FileInfo] $file) {
    $out = @(Native { & $Cli component $mount $file.FullName })
    return [pscustomobject]@{
        Present = [bool]($out | Where-Object { $_ -match ': present,' })
        Bytes = [int64](($out | Where-Object { $_ -match ': (present|not found), (\d+) bytes' } | Select-Object -First 1) -replace '.*: (present|not found), (\d+) bytes.*', '$2')
        Drivers = @($out | Where-Object { $_ -match '^\s+driver\s' }).Count
    }
}

# The shipped catalog: one recipe per deep entry.
$catalog = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\resources\catalog\components.json'), [System.Text.Encoding]::UTF8) | ConvertFrom-Json
foreach ($c in $catalog.components) {
    if (-not $c.driverClasses) { continue }
    $recipe = [ordered]@{ title = $c.id; driverClasses = @($c.driverClasses) }
    [System.IO.File]::WriteAllText((Join-Path $work "recipes\$($c.id).json"), ($recipe | ConvertTo-Json -Depth 4), (New-Object System.Text.UTF8Encoding $false))
}
$recipes = @(Get-ChildItem (Join-Path $work 'recipes') -Filter '*.json' | Sort-Object Name)

Say ("=== lab_deep_removal " + (Get-Date -Format s) + " ($($recipes.Count) deep entries, index $Index" + $(if ($Lcu) { $(if ($LcuFirst) { ', after ' } else { ', then ' }) + (Split-Path $Lcu -Leaf) } else { '' }) + ")")
$wim = Join-Path $work 'pro.wim'
Native { & $Cli export $source $Index $wim } | Out-Null
$before = Mb $wim
Say "  baseline export: $before MB"
$mounted = $false
try {
    Native { & $Cli mount $wim 1 $mount } | Select-Object -Last 1 | ForEach-Object { Say "  $_" }
    if ($LASTEXITCODE -ne 0) { throw "mount failed ($LASTEXITCODE)" }
    $mounted = $true
    if ($Lcu -and $LcuFirst) {
        AddLcu
        ScanHealth 'after the cumulative update'
    }

    foreach ($file in $recipes) {
        $p = Probe $file
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $out = @(Native { & $Cli component $mount $file.FullName --remove })
        $code = $LASTEXITCODE
        $after = Probe $file
        Check ("{0,-14} {1,3} driver(s), {2,6:N1} MB, exit {3}, {4} s, left after: {5}" -f $file.BaseName, $p.Drivers, ($p.Bytes / 1MB), $code,
            [int]$watch.Elapsed.TotalSeconds, $after.Drivers) ($code -eq 0 -and $p.Present -and -not $after.Present)
        $out | Where-Object { $_ -match 'error' } | Select-Object -First 3 | ForEach-Object { Say ("        " + $_.Trim()) }
    }
    $dual = @(Get-ChildItem (Join-Path $mount 'Windows\WinSxS') -Directory -Filter '*_dual_mdm*' -ErrorAction SilentlyContinue).Count
    Check "no modem payload left in WinSxS ($dual)" ($dual -eq 0)
    $repo = @(Get-ChildItem (Join-Path $mount 'Windows\System32\DriverStore\FileRepository') -Directory -Filter 'mdm*' -ErrorAction SilentlyContinue).Count
    Check "no modem package left in the driver store ($repo)" ($repo -eq 0)
    ScanHealth 'after the deep removal'

    if ($Lcu -and -not $LcuFirst) {
        AddLcu
        ScanHealth 'after the cumulative update'
        foreach ($file in $recipes) {
            $back = Probe $file
            Say ("  after the update: {0,-14} {1} driver(s) present" -f $file.BaseName, $back.Drivers)
        }
    }

    Native { & $Cli unmount $mount --commit } | Select-Object -Last 1 | ForEach-Object { Say "  $_" }
    $mounted = $false
    Check 'commit' ($LASTEXITCODE -eq 0)
    $exported = Join-Path $work 'after.wim'
    Native { & $Cli export $wim 1 $exported } | Out-Null
    Say ("  export after: $(Mb $exported) MB (was $before MB)")
    Native { & $Cli verify $exported } | Out-Null
    Check 'the exported WIM verifies' ($LASTEXITCODE -eq 0)
} finally {
    if ($mounted) { Native { & $Cli unmount $mount --discard } | Out-Null }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    Check 'work files deleted' (-not (Test-Path $work))
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
