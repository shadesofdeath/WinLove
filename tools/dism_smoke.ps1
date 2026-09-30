<#
.SYNOPSIS
  DISM smoke test (Faz 2.4) - run in an ELEVATED PowerShell:
    powershell -ExecutionPolicy Bypass -File tools\dism_smoke.ps1
  Mounts one edition of the lab copy READ-ONLY, lists features/packages/capabilities, unmounts
  with DISCARD and checks that nothing stays mounted. Never touches the source ISO.
  Output: console + build\lab\out\dism-smoke.json (AI sessions read this file).
#>
param(
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [int] $Index = 4,
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force (Join-Path $Lab 'out') | Out-Null
Start-Transcript -Path (Join-Path $Lab 'out\dism-smoke.log') -Force | Out-Null

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this in an elevated (Administrator) PowerShell.' }

$wim = Join-Path $Lab 'iso\sources\install.wim'
$mount = Join-Path $Lab 'mount\0'
$report = [ordered]@{ started = (Get-Date).ToString('s'); wim = $wim; index = $Index; steps = @() }

function Step([string] $name, [scriptblock] $block) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "==> $name" -ForegroundColor Cyan
    $output = & $block
    $code = $LASTEXITCODE
    $script:report.steps += [ordered]@{ step = $name; exit = $code; seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1) }
    if ($code -ne 0) { throw "$name failed (exit $code): $output" }
    return $output
}

try {
    if (-not (Test-Path $wim)) { throw "Lab not prepared: run tools\lab_setup.ps1 first" }
    Step 'cleanup stale mounts' { & $Cli cleanup }
    if (Test-Path $mount) { Remove-Item $mount -Recurse -Force }
    Step "mount index $Index read-only" { & $Cli mount $wim $Index $mount --readonly } | Out-Null
    $features = Step 'features' { & $Cli features $mount --json } | Out-String | ConvertFrom-Json
    $packages = Step 'packages' { & $Cli packages $mount --json } | Out-String | ConvertFrom-Json
    $capabilities = Step 'capabilities' { & $Cli capabilities $mount --json } | Out-String | ConvertFrom-Json
    $report.features = @{ total = $features.Count; enabled = @($features | Where-Object state -eq 'Installed').Count }
    $report.packages = $packages.Count
    $report.capabilities = @{ total = $capabilities.Count; installed = @($capabilities | Where-Object state -eq 'Installed').Count }
    $report.sampleFeatures = @($features | Select-Object -First 10)
}
finally {
    if (Test-Path $mount) {
        try { Step 'unmount (discard)' { & $Cli unmount $mount --discard } | Out-Null } catch { Write-Warning $_ }
    }
    try {
        $mounts = & $Cli mounts --json | Out-String | ConvertFrom-Json
        $report.remainingMounts = @($mounts).Count
    } catch {
        $report.remainingMounts = "unknown: $_"
    }
    $report.finished = (Get-Date).ToString('s')
    $out = Join-Path $Lab 'out\dism-smoke.json'
    $report | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $out
    Write-Host "`nreport -> $out"
    $report | ConvertTo-Json -Depth 3
    Stop-Transcript | Out-Null
}
