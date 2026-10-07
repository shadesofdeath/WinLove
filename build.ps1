<#
.SYNOPSIS
  WinLove single-entry build script. Loads the VS developer environment itself.

.EXAMPLE
  ./build.ps1                     # generate + layer check + Debug build
  ./build.ps1 -Config Release
  ./build.ps1 -Test               # + unit tests
  ./build.ps1 -Test -Integration  # + integration tests (elevated shell, build\lab)
  ./build.ps1 -Gen                # only run code generators
  ./build.ps1 -Clean              # delete the build directory of the chosen config first
  ./build.ps1 -Target wl_ui       # build a single CMake target (fast iteration)
  ./build.ps1 -Dist               # Release build + tests, then copy WinLove.exe / wlcli.exe to dist\ (what the user runs)
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Debug',
    [switch] $Test,
    [switch] $Integration,
    [switch] $Gen,
    [switch] $Clean,
    [string] $Target,
    [switch] $Dist
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
if ($Dist) { $Config = 'Release'; $Test = $true }
$Preset = "x64-$($Config.ToLower())"

function Invoke-Step([string] $Name, [scriptblock] $Block) {
    Write-Host "==> $Name" -ForegroundColor Cyan
    & $Block | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Name failed (exit $LASTEXITCODE)" }
}

function Import-VsDevEnvironment {
    if ($env:VCINSTALLDIR -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio with C++ tools not found.' }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    # vcvars itself calls vswhere by name; stderr is silenced because PowerShell 5.1 turns it into errors.
    $env:PATH = "$(Split-Path $vswhere);$env:PATH"
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

Push-Location $Root
try {
    $env:PYTHONUTF8 = '1'
    Invoke-Step 'generate (tokens, icons, strings, scripts)' { python tools/gen_all.py }
    if ($Gen) { return }
    Invoke-Step 'layer check' { python tools/check_layers.py }

    Import-VsDevEnvironment
    if ($Clean -and (Test-Path "build/$Preset")) { Remove-Item -Recurse -Force "build/$Preset" }
    Invoke-Step "configure ($Preset)" { cmake --preset $Preset --log-level=WARNING }
    # The user may be running an exe from the build folder: Windows cannot overwrite a running
    # exe but can rename it, so move locked outputs aside and link fresh ones.
    foreach ($exe in "build/$Preset/bin/WinLove.exe", "build/$Preset/bin/wlcli.exe") {
        if (Test-Path $exe) {
            try { [IO.File]::Open((Resolve-Path $exe), 'Open', 'ReadWrite', 'None').Close() }
            catch {
                Remove-Item "$exe.old" -Force -ErrorAction SilentlyContinue
                Rename-Item $exe ((Split-Path $exe -Leaf) + '.old')
                Write-Host "note: $exe is running; renamed to .old" -ForegroundColor Yellow
            }
        }
    }
    if ($Target) {
        Invoke-Step "build $Target ($Preset)" { cmake --build --preset $Preset --target $Target }
    } else {
        Invoke-Step "build ($Preset)" { cmake --build --preset $Preset }
    }

    if ($Test) {
        Invoke-Step 'unit tests' { ctest --preset $Preset -L unit }
    }
    if ($Integration) {
        $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
        if (-not $admin) { throw 'Integration tests need an elevated (Administrator) PowerShell.' }
        Invoke-Step 'integration tests' { ctest --preset $Preset -L integration }
    }
    if ($Dist) {
        New-Item -ItemType Directory -Force dist | Out-Null
        # The user usually has dist\WinLove.exe open: move a running copy aside instead of failing.
        foreach ($exe in 'dist/WinLove.exe', 'dist/wlcli.exe') {
            if (Test-Path $exe) {
                try { [IO.File]::Open((Resolve-Path $exe), 'Open', 'ReadWrite', 'None').Close() }
                catch {
                    Remove-Item "$exe.old" -Force -ErrorAction SilentlyContinue
                    Rename-Item $exe ((Split-Path $exe -Leaf) + '.old')
                    Write-Host "note: $exe is running; renamed to .old (restart WinLove to use the new build)" -ForegroundColor Yellow
                }
            }
        }
        Copy-Item "build/$Preset/bin/WinLove.exe", "build/$Preset/bin/wlcli.exe" dist/ -Force
        Write-Host "OK  dist\WinLove.exe (Release)" -ForegroundColor Green
    }
    Write-Host "OK  build/$Preset/bin" -ForegroundColor Green
}
finally {
    Pop-Location
}
