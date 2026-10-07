<#
  D-080 lab: the WinRE of an install image brought up to date with wlcli winre-update (elevated).
  Exports edition -Edition of the lab's setup media into build\lab\work\winre, mounts it, updates
  Windows\System32\Recovery\Winre.wim with the Safe OS dynamic update (and the LCU's servicing
  stack), checks the new WinRE version, commits. -Keep leaves the install.wim for a VM test.
  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe",
    [int] $Edition = 4,
    [string] $SafeOs = '',
    [string] $Lcu = '',
    [switch] $Keep
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$Cli = [System.IO.Path]::GetFullPath($Cli)
$work = Join-Path $Lab 'work\winre'
$mount = Join-Path $Lab 'mount\winre'
$log = Join-Path $Lab 'out\lab-winre.log'
if (-not $SafeOs) { $SafeOs = @(Get-ChildItem (Join-Path $Lab 'updates\du') -Filter 'windows11.0-kb5125758-*.cab')[0].FullName }
if (-not $Lcu) { $Lcu = @(Get-ChildItem (Join-Path $Lab 'updates') -Filter 'windows11.0-kb5129195-*.msu')[0].FullName }
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
Remove-Item $log -ErrorAction SilentlyContinue
$failed = 0
function Say([string] $text) { Add-Content -Path $log -Value $text -Encoding UTF8; Write-Host $text }
function Check([string] $what, [bool] $ok) { if ($ok) { Say ('PASS  ' + $what) } else { Say ('FAIL  ' + $what); $script:failed++ } }
function Run([string[]] $arguments) {
    Say ('> wlcli ' + ($arguments -join ' '))
    $output = & $Cli @arguments 2>&1 | ForEach-Object { "$_" }
    $script:lastExit = $LASTEXITCODE
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export|winre)\s+)?\d+%\s*$' } | Select-Object -Last 6 | ForEach-Object { Say ('    ' + $_.TrimEnd()) }
    $script:lastOutput = $output -join "`n"
}

Say ('=== lab_winre ' + (Get-Date -Format s))
Say ('Safe OS: ' + $SafeOs)
Say ('LCU:     ' + $Lcu)
foreach ($dir in $work, $mount) { if (Test-Path $dir) { Remove-Item $dir -Recurse -Force } }
New-Item -ItemType Directory -Force $work, $mount | Out-Null
try {
    Run @('export', (Join-Path $Lab 'setup\sources\install.wim'), "$Edition", "$work\install.wim")
    Run @('mount', "$work\install.wim", '1', $mount)
    Check "mount edition $Edition" ($script:lastExit -eq 0)
    $before = (Get-Item -Force (Join-Path $mount 'Windows\System32\Recovery\Winre.wim')).Length
    $started = Get-Date
    Run @('winre-update', $mount, "--safeos=$SafeOs", "--lcu=$Lcu")
    $seconds = [int]((Get-Date) - $started).TotalSeconds
    Check "winre-update ($seconds s)" ($script:lastExit -eq 0)
    $line = ($script:lastOutput -split "`n" | Where-Object { $_ -match '^WinRE ' } | Select-Object -Last 1)
    if ($line -match 'WinRE (\S+) -> (\S+)') {
        Check ("WinRE version moved on: " + $Matches[1] + ' -> ' + $Matches[2]) ([version]$Matches[2] -gt [version]$Matches[1])
    } else {
        Check 'WinRE version reported' $false
    }
    $item = Get-Item -Force (Join-Path $mount 'Windows\System32\Recovery\Winre.wim')
    Check ('Winre.wim back in place, hidden + system (' + $before + ' -> ' + $item.Length + ' bytes)') (($item.Attributes -band [IO.FileAttributes]::Hidden) -and ($item.Attributes -band [IO.FileAttributes]::System))
    Run @('unmount', $mount, '--commit')
    Check 'committed' ($script:lastExit -eq 0)
} finally {
    if (Test-Path (Join-Path $mount 'Windows')) { Run @('unmount', $mount, '--discard') }
    if (-not $Keep) { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
exit $failed
