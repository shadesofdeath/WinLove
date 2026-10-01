<#
.SYNOPSIS
  Engine check for the P07 package-level components (D-059): on a COPY of one edition of the lab
  install.wim, every catalog entry with packages (resources\catalog\components.json; or every
  recipe in -Recipes) is removed with `wlcli component <mount> <recipe> --remove`,
  one by one; each listed package must be gone from the image's CBS list afterwards. Then DISM
  /ScanHealth on the image (the component store must stay healthy), commit, and a fresh export
  measures what the ISO gains. Nothing outside build\lab is touched; the test ISO is never read.

    powershell -ExecutionPolicy Bypass -File tools\lab_cbs_removal.ps1 [-Recipes <folder>] [-Index 4] [-SkipScanHealth]
  Needs an elevated PowerShell. About 30-40 minutes.
  Log: build\lab\out\cbs-removal-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI
  (tests/base/ScriptTests.cpp).
#>
param(
    [string] $Recipes = '',
    [int] $Index = 4,              # Windows 11 Pro in the 25H2 test ISO
    [switch] $SkipScanHealth,
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-release\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
if (-not (Test-Path $source)) { $source = Join-Path $Lab 'iso\sources\install.wim' }
$work = Join-Path $Lab 'work\cbs'
$mount = Join-Path $Lab 'mount\cbs'
$log = Join-Path $Lab 'out\cbs-removal-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1 -Config Release" }
if (-not (Test-Path $source)) { throw "Lab image missing ($source)" }
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work, $mount, (Join-Path $Lab 'out') | Out-Null
if (-not $Recipes) {
    # The shipped catalog: one recipe file per entry that has packages.
    $Recipes = Join-Path $work 'recipes'
    New-Item -ItemType Directory -Force $Recipes | Out-Null
    $catalog = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\resources\catalog\components.json'), [System.Text.Encoding]::UTF8) | ConvertFrom-Json
    foreach ($c in $catalog.components) {
        if (-not $c.packages) { continue }
        $recipe = [ordered]@{ title = $c.id; packages = @($c.packages); paths = @($c.paths | Where-Object { $_ }); registry = @() }
        [System.IO.File]::WriteAllText((Join-Path $Recipes ($c.id + '.json')), ($recipe | ConvertTo-Json -Depth 4), (New-Object System.Text.UTF8Encoding $false))
    }
}
$Recipes = [System.IO.Path]::GetFullPath($Recipes)
$recipeFiles = @(Get-ChildItem $Recipes -Filter '*.json' | Sort-Object Name)
if ($recipeFiles.Count -eq 0) { throw "No recipes in $Recipes" }

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

Say ("=== lab_cbs_removal " + (Get-Date -Format s) + " ($($recipeFiles.Count) recipes, index $Index)")
$wim = Join-Path $work 'pro.wim'
Native { & $Cli export $source $Index $wim } | Out-Null
$before = Mb $wim
Say "  baseline export: $before MB"
$mounted = $false
try {
    Native { & $Cli mount $wim 1 $mount } | Select-Object -Last 1 | ForEach-Object { Say "  $_" }
    if ($LASTEXITCODE -ne 0) { throw "mount failed ($LASTEXITCODE)" }
    $mounted = $true

    $results = @()
    foreach ($file in $recipeFiles) {
        $recipe = Get-Content $file.FullName -Raw | ConvertFrom-Json
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $out = @(Native { & $Cli component $mount $file.FullName --remove })
        $code = $LASTEXITCODE
        $seconds = [int]$watch.Elapsed.TotalSeconds
        $warnings = @($out | Where-Object { $_ -match 'error|warn|refused|not removed' })
        # Every listed family: no identity left in an installed / staged state.
        $cbs = @(Native { & $Cli cbs $mount })
        $left = @()
        foreach ($family in $recipe.packages) {
            $hits = @($cbs | Where-Object { $_ -match ('\s' + [regex]::Escape($family) + '~') -and $_ -match '0x(70|40|50|60|65)' })
            if ($hits.Count -gt 0) { $left += $family }
        }
        $ok = ($code -eq 0 -and $left.Count -eq 0)
        Check ("{0,-22} exit {1}, {2,4} s{3}" -f $file.BaseName, $code, $seconds, $(if ($left.Count) { ", left: " + ($left -join ' ') } else { '' })) $ok
        foreach ($w in $warnings | Select-Object -First 4) { Say ("        " + $w.Trim()) }
        $results += [pscustomobject]@{ Id = $file.BaseName; Exit = $code; Seconds = $seconds; Left = ($left -join ' ') }
    }
    $results | Export-Csv -Path (Join-Path $Lab 'out\cbs-removal-results.csv') -NoTypeInformation -Encoding UTF8

    if (-not $SkipScanHealth) {
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $scan = @(Native { & dism.exe /English "/Image:$mount" /Cleanup-Image /ScanHealth })
        $line = ($scan | Where-Object { $_ -match 'component store|corruption|repairable' } | Select-Object -Last 1)
        Check ("ScanHealth after the removals: " + "$line".Trim() + " (" + [int]$watch.Elapsed.TotalSeconds + " s)") ($LASTEXITCODE -eq 0 -and "$line" -match 'No component store corruption')
    }
    $pkgs = @(Native { & dism.exe /English "/Image:$mount" /Get-Packages })
    Check ("DISM still lists the packages ($(@($pkgs | Where-Object { $_ -match '^Package Identity' }).Count))") ($LASTEXITCODE -eq 0)

    Native { & $Cli unmount $mount --commit } | Select-Object -Last 1 | ForEach-Object { Say "  $_" }
    $mounted = $false
    Check 'commit' ($LASTEXITCODE -eq 0)
    $after = Join-Path $work 'after.wim'
    Native { & $Cli export $wim 1 $after } | Out-Null
    Say ("  export after the removals: $(Mb $after) MB (was $before MB, -" + ($before - (Mb $after)) + " MB)")
    Native { & $Cli verify $after } | Out-Null
    Check 'the exported WIM verifies' ($LASTEXITCODE -eq 0)
} finally {
    if ($mounted) { Native { & $Cli unmount $mount --discard } | Out-Null }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    Check 'work files deleted' (-not (Test-Path $work))
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
