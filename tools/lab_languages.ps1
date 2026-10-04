<#
.SYNOPSIS
  Engine check for D-061 (language files from Windows Update) on a real image:
    A. Pro: the files the way uupdump.net names them (a hand-downloaded folder, LP as .esd,
       features "-Package-amd64.cab", express metadata next to them) queued as AddPackage
       "language" + the UI language -> wlcli apply. This is what failed with 0x800F0912.
    B. Home: wlcli uup-languages finds the image's own build, picks the language pack, every
       feature and the languages of the components THIS image has (--packages-of), downloads them
       SHA-256 checked under the names DISM wants, and the same apply runs from that folder.
  Each edition is a copy in build\lab\work, mounted into build\lab\mount\languages and discarded
  at the end. The ISO and build\lab\setup are only read.

  Needs an elevated PowerShell (about 15 minutes, 25 GB free, internet) and:
    build\lab\setup\sources\install.wim           (wlcli extract-all <test.iso> build\lab\setup)
    build\lab\lang\26200.8037\en-us               (UUP-named en-us files; -UupFolder to point elsewhere)

    powershell -ExecutionPolicy Bypass -File tools\lab_languages.ps1
  The output is also written to build\lab\out\languages-test.log (UTF-8).

  Keep this file plain ASCII: Windows PowerShell reads a BOM-less script as ANSI.
#>
param(
    [string] $Language = 'en-US',
    [string] $UupFolder = '',
    [string] $Lab = (Join-Path $PSScriptRoot '..\build\lab'),
    [string] $Cli = "$PSScriptRoot\..\build\x64-debug\bin\wlcli.exe"
)
$ErrorActionPreference = 'Stop'
$Lab = [System.IO.Path]::GetFullPath($Lab)
$source = Join-Path $Lab 'setup\sources\install.wim'
if (-not $UupFolder) { $UupFolder = Join-Path $Lab ('lang\26200.8037\' + $Language.ToLower()) }
$work = Join-Path $Lab 'work\languages'
$mount = Join-Path $Lab 'mount\languages'
$downloads = Join-Path $Lab ('lang\uup\' + $Language.ToLower())
$log = Join-Path $Lab 'out\languages-test.log'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run this from an elevated (Administrator) PowerShell.' }
if (-not (Test-Path $Cli)) { throw "wlcli.exe not built yet: run ./build.ps1" }
if (-not (Test-Path $source)) { throw "No lab install.wim: run  wlcli extract-all <test.iso> build\lab\setup  first" }
if (-not (Test-Path $UupFolder)) { throw "No UUP-named language folder: $UupFolder" }
foreach ($dir in 'out', 'mount') { New-Item -ItemType Directory -Force (Join-Path $Lab $dir) | Out-Null }
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force $work, $mount | Out-Null
Remove-Item $log -ErrorAction SilentlyContinue

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
    $output | Where-Object { $_.Trim() -and $_ -notmatch '^\s*((mount|commit|discard|export|download)\s+)?\d+%\s*$' } |
        ForEach-Object { Say ("  " + ($_ -replace '^(\s*(mount|commit|discard|export|download)\s+\d+%)+', '').TrimEnd()) }
    Say ("  (exit $script:lastExit, " + [int]$watch.Elapsed.TotalSeconds + " s)")
    return $output
}
function Json([string[]] $arguments) {
    $text = (Native { & $Cli @arguments } | Out-String)
    $script:jsonExit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0 -or -not $text.Trim()) { return $null }
    return ($text | ConvertFrom-Json)
}
function Check([string] $what, [bool] $ok) {
    if ($ok) { Say "PASS  $what" } else { Say "FAIL  $what"; $script:failed++ }
}
function WriteUtf8([string] $path, [string] $text) {
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding $false))
}
# Every language file of a folder (express metadata left out: the engine must not need it) as
# AddPackage "language", plus the UI language. The planner puts them in install order.
function LanguageChangeSet([string] $folder, [string] $path) {
    $operations = @()
    foreach ($f in Get-ChildItem $folder -File | Where-Object { $_.Extension -in '.cab', '.esd' }) {
        if ($f.Name -match '_[0-9a-f]{8}\.cab$' -or $f.Name -match '-Package\.cab$') { continue }
        $operations += @{ kind = 'addPackage'; target = $f.FullName; value = 'language'; risk = 'low' }
    }
    $intl = '{"ui":"' + $Language + '"}'
    $operations += @{ kind = 'setIntl'; target = 'intl'; value = $intl; risk = 'low' }
    $doc = [ordered]@{ format = 'winlove.changeset'; version = 1; operations = $operations }
    WriteUtf8 $path ($doc | ConvertTo-Json -Depth 5)
    return ($operations.Count - 1)
}
function Verify([string] $edition, [int] $files) {
    $intl = Json @('intl', $mount, '--json')
    Check "$edition : $Language installed (languages $($intl.languages -join ','))" ($intl.languages -contains $Language)
    Check "$edition : UI language is $Language ($($intl.ui))" ($intl.ui -eq $Language)
    $packages = @(Native { & $Cli packages $mount }) | Where-Object { $_ -match [regex]::Escape($Language) -or $_ -match ('-' + $Language.ToLower() + '-Package') }
    Check "$edition : $($packages.Count) $Language package(s) in the image (queued files: $files)" ($packages.Count -ge $files)
    foreach ($k in 'Basic', 'Handwriting', 'OCR', 'TextToSpeech') {
        Check "$edition : LanguageFeatures-$k installed" (@($packages | Where-Object { $_ -match "LanguageFeatures-$k-" }).Count -ge 1)
    }
}

Say ("=== lab_languages " + (Get-Date -Format s))
try {
    # A. Pro, from the hand-downloaded UUP folder.
    Run @('languages', $UupFolder) | Out-Null
    Run @('export', $source, '4', "$work\pro.wim") | Out-Null
    Check 'export Pro' ($script:lastExit -eq 0)
    Run @('mount', "$work\pro.wim", '1', $mount) | Out-Null
    Check 'mount Pro' ($script:lastExit -eq 0)
    $before = Json @('intl', $mount, '--json')
    Say "  before: UI $($before.ui), languages $($before.languages -join ',')"
    $n = LanguageChangeSet $UupFolder "$work\uup-names.json"
    Run @('plan', "$work\uup-names.json") | Out-Null
    Run @('apply', "$work\uup-names.json", $mount) | Out-Null
    Check "apply from UUP names: $n language file(s) + UI language, no step failed" ($script:lastExit -eq 0)
    Verify 'Pro (UUP names)' $n
    Run @('unmount', $mount, '--discard') | Out-Null
    Check 'Pro unmounted' ($script:lastExit -eq 0)

    # B. Home, found and downloaded by the engine for this image.
    Run @('export', $source, '1', "$work\home.wim") | Out-Null
    Run @('mount', "$work\home.wim", '1', $mount) | Out-Null
    Check 'mount Home' ($script:lastExit -eq 0)
    Run @('uup-languages', '26200.8037') | Out-Null
    Check 'uupdump.net lists the languages of 26200.8037' ($script:lastExit -eq 0)
    Run @('uup-languages', '26200.8037', "--lang=$Language",
          '--parts=pack,basic,fonts,handwriting,ocr,tts,speech,components', "--packages-of=$mount", "--download=$downloads") | Out-Null
    Check "download of $Language for this image (SHA-256 checked)" ($script:lastExit -eq 0)
    $listing = @(Native { & $Cli languages $downloads })
    Check 'downloaded files carry the names DISM wants' (@($listing | Where-Object { $_ -match 'UUP name' }).Count -eq 0)
    $n = LanguageChangeSet $downloads "$work\downloaded.json"
    Run @('apply', "$work\downloaded.json", $mount) | Out-Null
    Check "apply from the download: $n language file(s) + UI language, no step failed" ($script:lastExit -eq 0)
    Verify 'Home (downloaded)' $n
    Run @('unmount', $mount, '--discard') | Out-Null
    Check 'Home unmounted' ($script:lastExit -eq 0)
} catch {
    Say ("ERROR  " + $_.Exception.Message + " (line " + $_.InvocationInfo.ScriptLineNumber + ")")
    $failed++
} finally {
    if (Test-Path (Join-Path $mount 'Windows')) {
        Run @('unmount', $mount, '--discard') | Out-Null
    }
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    Check 'lab WIMs and work files deleted' (-not (Test-Path $work))
}
Say ("`n=== " + $(if ($failed -eq 0) { 'ALL PASSED' } else { "$failed FAILED" }) + " (log: $log)")
