<#
.SYNOPSIS
  Which earlier change keeps a cumulative update (.msu, UUP based) from installing into an image?
  Seen after D-060 deep removal and D-063 Defender removal: "An error occurred applying the
  Unattend.xml file from the .msu package" (CBS: "Active offline session not registered",
  0x800401E3). Each case runs on a fresh copy of edition 4: the change through wlcli apply, then the
  LCU in a second wlcli apply (a new DISM session, as on another day), result recorded.
    T1  nothing before the LCU
    T2  one registry value (offline hive write)
    T3  a service start type (SYSTEM hive)
    T4  the Windows Security app removed natively
    T5  files removed (component recipe with paths only)
  Copies in build\lab\work\lcuafter, mounted into build\lab\mount\lcuafter, discarded each time.

    powershell -ExecutionPolicy Bypass -File tools\lab_lcu_after.ps1 [-Cases T1,T2]   (elevated, ~10 min per case)

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string[]] $Cases = @('T1', 'T2', 'T3', 'T4', 'T5'),
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
$work = Join-Path $Lab 'work\lcuafter'
$mount = Join-Path $Lab 'mount\lcuafter'
$log = Join-Path $Lab 'out\lcu-after-test.log'
$lcu = Get-ChildItem (Join-Path $Lab 'updates') -Filter 'windows11.0-kb5129195-x64*.msu' | Select-Object -First 1
if (-not $lcu) { throw 'No LCU in build\lab\updates' }
foreach ($dir in 'out', 'mount') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
Remove-Item $log -ErrorAction SilentlyContinue

function Say([string] $text) { Add-Content -Path $log -Value $text -Encoding UTF8; Write-Host $text }
function Native([scriptblock] $command) {
    $previous = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $output = & $command 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = $previous
    return $output
}
function Run([string[]] $arguments) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $output = @(Native { & $Cli @arguments })
    $script:lastExit = $LASTEXITCODE
    $text = ($output | Where-Object { $_ -match 'FAILED|error|step\(s\) ran' }) -join ' | '
    Say ("  wlcli " + $arguments[0] + " -> exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s  " + $text)
}
function WriteUtf8([string] $path, [string] $text) { [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding $false)) }
function ChangeSet([string] $path, [object[]] $operations) {
    WriteUtf8 $path (([ordered]@{ format = 'winlove.changeset'; version = 1; operations = $operations }) | ConvertTo-Json -Depth 6)
}

$changes = @{
    T2 = @([ordered]@{ kind = 'setRegistryValue'; target = 'HKLM\SOFTWARE\Policies\Microsoft\Windows\DataCollection::AllowTelemetry'; value = 'dword:00000000'; risk = 'low' })
    T3 = @([ordered]@{ kind = 'setServiceStart'; target = 'DiagTrack'; value = 'disabled'; risk = 'low' })
    T4 = @([ordered]@{ kind = 'removeComponent'; target = 'sechealth'; value = '{"title":"Windows Security app","appx":["Microsoft.SecHealthUI"]}'; risk = 'high' })
    T5 = @([ordered]@{ kind = 'removeComponent'; target = 'files'; value = '{"title":"Defender files","paths":["Program Files\\Windows Defender","ProgramData\\Microsoft\\Windows Defender"]}'; risk = 'high' })
}

Say ("=== lab_lcu_after " + (Get-Date -Format s) + "  LCU " + $lcu.Name)
foreach ($case in $Cases) {
    Say "`n--- $case"
    try {
        if (Test-Path $work) { Remove-Item $work -Recurse -Force }
        New-Item -ItemType Directory -Force $work, $mount | Out-Null
        Run @('export', $source, '4', "$work\pro.wim")
        Run @('mount', "$work\pro.wim", '1', $mount)
        if ($changes.ContainsKey($case)) {
            ChangeSet "$work\change.json" $changes[$case]
            Run @('apply', "$work\change.json", $mount)
        }
        ChangeSet "$work\lcu.json" @([ordered]@{ kind = 'addPackage'; target = $lcu.FullName; value = 'lcu'; risk = 'low' })
        Run @('apply', "$work\lcu.json", $mount)
        Say ("RESULT $case : LCU " + $(if ($script:lastExit -eq 0) { 'INSTALLED' } else { 'FAILED' }))
    } catch {
        Say ("ERROR  " + $_.Exception.Message)
    } finally {
        if (Test-Path (Join-Path $mount 'Windows')) { Run @('unmount', $mount, '--discard') }
        Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
Say "`n=== done"
