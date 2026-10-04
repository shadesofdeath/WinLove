<#
.SYNOPSIS
  Engine check for D-066 (Microsoft Store apps) on a real image: search, the app and its frameworks
  from Windows Update (SHA-256 checked), provisioned into a copy of edition 4 with wlcli appx-add,
  then listed among the image's provisioned apps. Downloads stay in build\lab\store (reused).

    powershell -ExecutionPolicy Bypass -File tools\lab_store.ps1 [-ProductId 9WZDNCRFHWM4]   (elevated)

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $ProductId = '9WZDNCRFHWM4',
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
$work = Join-Path $Lab 'work\store'
$mount = Join-Path $Lab 'mount\store'
$downloads = Join-Path $Lab "store\$ProductId"
$log = Join-Path $Lab 'out\store-test.log'
foreach ($dir in 'out', 'mount') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
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
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export|download)\s+)?\d+%\s*$' } | ForEach-Object { Say ("  " + $_.TrimEnd()) }
    Say ("  (exit $script:lastExit)")
    return $output
}
function Check([string] $what, [bool] $ok) { if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ } }

Say ("=== lab_store " + (Get-Date -Format s) + " $ProductId")
try {
    Run @('store-search', 'wikipedia') | Out-Null
    Check 'Store search answers' ($script:lastExit -eq 0)
    Run @('store-get', $ProductId, "--download=$downloads") | Out-Null
    Check 'app and frameworks downloaded, SHA-256 checked' ($script:lastExit -eq 0)
    $app = Get-ChildItem $downloads -File | Where-Object { $_.Name -notmatch '^Microsoft\.(VCLibs|WindowsAppRuntime|NET|UI\.Xaml)' } | Select-Object -First 1
    Check "the app's own package is there ($($app.Name))" ($null -ne $app)
    Run @('appx-info', $app.FullName) | Out-Null
    Check 'manifest read, dependencies found next to it' ($script:lastExit -eq 0)

    Run @('export', $source, '4', "$work\pro.wim") | Out-Null
    Run @('mount', "$work\pro.wim", '1', $mount) | Out-Null
    Check 'mount Pro' ($script:lastExit -eq 0)
    Run @('appx-add', $mount, $app.FullName) | Out-Null
    Check 'provisioned into the image' ($script:lastExit -eq 0)
    $name = ($app.Name -split '_')[0]
    $apps = @(Native { & $Cli appx $mount })
    Check "listed among the provisioned apps ($name)" (@($apps | Where-Object { $_ -match [regex]::Escape($name) }).Count -ge 1)
} catch {
    Say ("ERROR  " + $_.Exception.Message + " (line " + $_.InvocationInfo.ScriptLineNumber + ")")
    $failed++
} finally {
    if (Test-Path (Join-Path $mount 'Windows')) { Run @('unmount', $mount, '--discard') | Out-Null }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
