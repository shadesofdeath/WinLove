<#
  WinLove lab (D-153 ... D-155 install test): runs in the installed Windows at the first sign-in
  (lab_vm.ps1 -FirstLogon). Checks what the changeset put into the image: Group Policy (the values,
  Registry.pol read back by the Group Policy engine, gpresult), the right-click menu (registry and the
  shell's own verb list, each hidden item shown once to prove the hiding is what removes it), the
  removal gaps (the apps gone, Print Screen, file associations). Results: C:\ProgramData\WinLoveLab\check.txt
  and out\, copied onto the lab's WLDIAG disk; the menus are opened for the VNC screenshots.
  Keep this file plain ASCII.
#>
$ErrorActionPreference = 'Continue'
$base = 'C:\ProgramData\WinLoveLab'
$out = Join-Path $base 'out'
New-Item -ItemType Directory -Force $out | Out-Null
$report = Join-Path $base 'check.txt'
$script:fails = 0
function Say([string] $t) { Add-Content -Path $report -Value $t -Encoding UTF8; Write-Host $t }
function Check([string] $what, [bool] $ok, [string] $detail = '') {
    if ($ok) { Say "PASS  $what  $detail" } else { Say "FAIL  $what  $detail"; $script:fails++ }
}
function Info([string] $t) { Say "INFO  $t" }
function RegValue([string] $key, [string] $name) {
    try { return (Get-ItemProperty -LiteralPath $key -Name $name -ErrorAction Stop).$name } catch { return $null }
}
function HasValue([string] $key, [string] $name) {
    try { $k = Get-Item -LiteralPath $key -ErrorAction Stop; return @($k.GetValueNames()) -contains $name } catch { return $false }
}

Say ("=== WinLove lab check " + (Get-Date -Format s))
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
Info ("user " + $env:USERNAME + ", elevated: " + $admin + ", build " + [Environment]::OSVersion.Version)
$deadline = (Get-Date).AddMinutes(3)
while (-not (Get-Process explorer -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep 2 }
Start-Sleep 20

Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices; using System.Text;
public static class WlLab {
    [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
    static extern uint AssocQueryStringW(uint flags, uint str, string assoc, string extra, StringBuilder outText, ref uint outLen);
    public static string Assoc(string ext, uint what) {
        uint n = 2048; var sb = new StringBuilder(2048);
        uint hr = AssocQueryStringW(0, what, ext, what == 20 ? null : "open", sb, ref n);
        return hr == 0 ? sb.ToString() : ("(hr 0x" + hr.ToString("X8") + ")");
    }
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr w);
    public static void Key(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); keybd_event(vk, 0, 2, UIntPtr.Zero); }
    public static void ShiftF10() { keybd_event(0x10, 0, 0, UIntPtr.Zero); Key(0x79); keybd_event(0x10, 0, 2, UIntPtr.Zero); }
}
"@

# ---------------------------------------------------------------- 4. Group Policy
Say ''
Say '--- Group Policy'
$au = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU'
Check 'AutoUpdateCfg: NoAutoUpdate = 0' ((RegValue $au 'NoAutoUpdate') -eq 0)
Check 'AutoUpdateCfg: AUOptions = 2 (the option chosen, not the default 3)' ((RegValue $au 'AUOptions') -eq 2) ("= " + (RegValue $au 'AUOptions'))
$dc = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DataCollection'
Check 'AllowTelemetry = 1' ((RegValue $dc 'AllowTelemetry') -eq 1)
$ts = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services'
Check 'TS_DISABLE_CONNECTIONS disabled: fDenyTSConnections = 1' ((RegValue $ts 'fDenyTSConnections') -eq 1)
$ex = 'HKCU:\Software\Policies\Microsoft\Windows\Explorer'
Check 'user policy DisableSearchBoxSuggestions = 1 (this user)' ((RegValue $ex 'DisableSearchBoxSuggestions') -eq 1)
$gp = 'C:\Windows\System32\GroupPolicy'
Check 'Machine\Registry.pol' (Test-Path "$gp\Machine\Registry.pol") ("" + (Get-Item "$gp\Machine\Registry.pol" -ErrorAction SilentlyContinue).Length + " bytes")
Check 'User\Registry.pol' (Test-Path "$gp\User\Registry.pol") ("" + (Get-Item "$gp\User\Registry.pol" -ErrorAction SilentlyContinue).Length + " bytes")
Copy-Item "$gp\gpt.ini" $out -ErrorAction SilentlyContinue
Copy-Item "$gp\Machine\Registry.pol" "$out\Machine-Registry.pol" -ErrorAction SilentlyContinue
Copy-Item "$gp\User\Registry.pol" "$out\User-Registry.pol" -ErrorAction SilentlyContinue
Info ('gpt.ini: ' + ((Get-Content "$gp\gpt.ini" -ErrorAction SilentlyContinue) -join ' | '))

# The Group Policy engine itself: values deleted by hand come back from Registry.pol on gpupdate.
Remove-ItemProperty -LiteralPath $dc -Name 'AllowTelemetry' -ErrorAction SilentlyContinue
Remove-ItemProperty -LiteralPath $au -Name 'AUOptions' -ErrorAction SilentlyContinue
Remove-ItemProperty -LiteralPath $ex -Name 'DisableSearchBoxSuggestions' -ErrorAction SilentlyContinue
Check 'values deleted by hand before gpupdate' ((RegValue $dc 'AllowTelemetry') -eq $null -and (RegValue $ex 'DisableSearchBoxSuggestions') -eq $null)
$t0 = Get-Date
cmd /c "echo n| gpupdate /target:computer /force" 2>&1 | Out-File "$out\gpupdate-computer.txt" -Encoding UTF8
cmd /c "echo n| gpupdate /target:user /force" 2>&1 | Out-File "$out\gpupdate-user.txt" -Encoding UTF8
Info ("gpupdate took " + [int]((Get-Date) - $t0).TotalSeconds + " s")
Check 'gpupdate wrote AllowTelemetry back from Machine\Registry.pol' ((RegValue $dc 'AllowTelemetry') -eq 1)
Check 'gpupdate wrote AUOptions = 2 back' ((RegValue $au 'AUOptions') -eq 2)
Check 'gpupdate wrote the user policy back from User\Registry.pol' ((RegValue $ex 'DisableSearchBoxSuggestions') -eq 1)
gpresult /scope computer /v 2>&1 | Out-File "$out\gpresult-computer.txt" -Encoding UTF8
gpresult /scope user /v 2>&1 | Out-File "$out\gpresult-user.txt" -Encoding UTF8
gpresult /h "$out\gpresult.html" /f 2>&1 | Out-Null
$gc = (Get-Content "$out\gpresult-computer.txt" -Raw -ErrorAction SilentlyContinue)
$gu = (Get-Content "$out\gpresult-user.txt" -Raw -ErrorAction SilentlyContinue)
Check 'gpresult (computer) lists DataCollection\AllowTelemetry' ($gc -match 'DataCollection\\AllowTelemetry')
Check 'gpresult (computer) lists WindowsUpdate\AU\AUOptions' ($gc -match 'AU\\AUOptions')
Check 'gpresult (computer) lists Terminal Services\fDenyTSConnections' ($gc -match 'fDenyTSConnections')
Check 'gpresult (user) lists Explorer\DisableSearchBoxSuggestions' ($gu -match 'DisableSearchBoxSuggestions')
try {
    Get-WinEvent -LogName 'Microsoft-Windows-GroupPolicy/Operational' -MaxEvents 400 -ErrorAction Stop |
        Where-Object { $_.Level -le 3 } | Select-Object TimeCreated, Id, LevelDisplayName, Message |
        Format-List | Out-File "$out\gp-warnings.txt" -Encoding UTF8
    Info ('Group Policy log warnings/errors: ' + @(Get-WinEvent -LogName 'Microsoft-Windows-GroupPolicy/Operational' -MaxEvents 400 | Where-Object { $_.Level -le 3 }).Count)
} catch { Info ('Group Policy log: ' + $_.Exception.Message) }

# ---------------------------------------------------------------- 5. Context menu
Say ''
Say '--- Context menu'
$blocked = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Blocked'
$share = '{E2BF9676-5F8F-435C-97EB-11607A5BEDF7}'
$prev = '{596AB062-B4D2-4215-9F74-E9109B0A8153}'
$term = '{9F156763-7844-4DC4-B2B1-901F640F5155}'
$pin = 'Registry::HKEY_CLASSES_ROOT\*\shell\pintohomefile'
Check 'pintohomefile verb: ProgrammaticAccessOnly' (HasValue $pin 'ProgrammaticAccessOnly')
foreach ($c in $share, $prev, $term) { Check "Blocked $c" (HasValue $blocked $c) ("= " + (RegValue $blocked $c)) }
$explore = 'Registry::HKEY_CLASSES_ROOT\Folder\shell\explore'
Check 'Folder explore verb shown (no LegacyDisable / ProgrammaticAccessOnly)' (-not (HasValue $explore 'LegacyDisable') -and -not (HasValue $explore 'ProgrammaticAccessOnly'))
$mine = 'Registry::HKEY_CLASSES_ROOT\*\shell\WinLove.not-defteri-ile-a'
$cmdText = RegValue "$mine\command" '(default)'
Check 'own item on files: WinLove.not-defteri-ile-a\command' ($cmdText -like 'notepad.exe*%1*') ("= " + $cmdText)
$bg = 'Registry::HKEY_CLASSES_ROOT\Directory\Background\shell\WinLove.komut-istemi-burada'
$bgText = RegValue "$bg\command" '(default)'
Check 'own item on the folder background uses %V' ($bgText -like '*%V*') ("= " + $bgText)

# The shell's own list of a file's and a folder's verbs, each in a new process (the Blocked list
# is read once per process).
$lab = 'C:\Users\Public\WinLoveLab'
New-Item -ItemType Directory -Force $lab | Out-Null
Set-Content -Path "$lab\test.txt" -Value 'WinLove lab' -Encoding ASCII
$verbsScript = Join-Path $base 'verbs.ps1'
Set-Content -Path $verbsScript -Encoding ASCII -Value @(
    'param([string] $Dir, [string] $Name, [string] $Out)',
    '$item = (New-Object -ComObject Shell.Application).Namespace($Dir).ParseName($Name)',
    '@($item.Verbs() | ForEach-Object { ($_.Name -replace "&", "").Trim() } | Where-Object { $_ }) | Set-Content -Path $Out -Encoding UTF8')
$script:nv = 0
function Verbs([string] $dir, [string] $name) {
    $script:nv++
    $file = Join-Path $out ("verbs-{0:D2}.txt" -f $script:nv)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $verbsScript -Dir $dir -Name $name -Out $file | Out-Null
    return @(Get-Content $file -Encoding UTF8 -ErrorAction SilentlyContinue)
}
$fileVerbs = Verbs $lab 'test.txt'
$folderVerbs = Verbs 'C:\Users\Public' 'WinLoveLab'
Info ('file verbs (' + $fileVerbs.Count + '): ' + ($fileVerbs -join ' | '))
Info ('folder verbs (' + $folderVerbs.Count + '): ' + ($folderVerbs -join ' | '))
Check 'the shell lists our item for a file' (@($fileVerbs | Where-Object { $_ -like 'Not Defteri ile a*' }).Count -eq 1)

# Shown once: what appears is what the hiding takes away.
function Unhide([string] $what, [scriptblock] $undo, [scriptblock] $redo, [string] $dir, [string] $name, $before, [bool] $mustAppear = $true) {
    & $undo
    $now = Verbs $dir $name
    & $redo
    $new = @($now | Where-Object { $before -notcontains $_ })
    if ($mustAppear) { Check "hidden by WinLove: $what" ($new.Count -ge 1) ("(shown it adds: " + ($new -join ' | ') + ")") }
    else { Info ("$what shown adds to the shell's verb list: " + ($new -join ' | ')) }
}
$pinValue = RegValue $pin 'ProgrammaticAccessOnly'
Unhide 'pintohomefile (verb)' { Remove-ItemProperty -LiteralPath $pin -Name 'ProgrammaticAccessOnly' } `
    { New-ItemProperty -LiteralPath $pin -Name 'ProgrammaticAccessOnly' -Value $pinValue -PropertyType String -Force | Out-Null } $lab 'test.txt' $fileVerbs
foreach ($c in $share, $prev) {
    $v = RegValue $blocked $c
    Unhide "$c (handler)" { Remove-ItemProperty -LiteralPath $blocked -Name $c } `
        { New-ItemProperty -LiteralPath $blocked -Name $c -Value $v -PropertyType String -Force | Out-Null } $lab 'test.txt' $fileVerbs
}
$v = RegValue $blocked $term
Unhide "$term (Terminal, packaged)" { Remove-ItemProperty -LiteralPath $blocked -Name $term } `
    { New-ItemProperty -LiteralPath $blocked -Name $term -Value $v -PropertyType String -Force | Out-Null } 'C:\Users\Public' 'WinLoveLab' $folderVerbs $false
Check 'every hiding restored after the check' ((HasValue $pin 'ProgrammaticAccessOnly') -and (HasValue $blocked $share) -and (HasValue $blocked $prev) -and (HasValue $blocked $term))

# ---------------------------------------------------------------- 3. Removal gaps
Say ''
Say '--- Removal gaps'
foreach ($app in 'Microsoft.ScreenSketch', 'Microsoft.Windows.Photos', 'Microsoft.WindowsNotepad') {
    $left = @(Get-AppxPackage -AllUsers -Name $app -ErrorAction SilentlyContinue) + @(Get-AppxProvisionedPackage -Online -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -eq $app })
    Check "$app removed" ($left.Count -eq 0)
}
$kb = 'HKCU:\Control Panel\Keyboard'
$deadline = (Get-Date).AddMinutes(3)
while ((RegValue $kb 'PrintScreenKeyForSnippingEnabled') -eq $null -and (Get-Date) -lt $deadline) { Start-Sleep 5 }
Check 'Print Screen copies the screen: PrintScreenKeyForSnippingEnabled = 0' ((RegValue $kb 'PrintScreenKeyForSnippingEnabled') -eq 0) ("= " + (RegValue $kb 'PrintScreenKeyForSnippingEnabled'))
Check 'Photo Viewer registered (PhotoViewer.FileAssoc.Jpeg)' (Test-Path 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\PhotoViewer.FileAssoc.Jpeg\shell\open\command')
foreach ($ext in '.jpg', '.png', '.gif', '.bmp') {
    $command = [WlLab]::Assoc($ext, 1)
    Check "$ext opens with Photo Viewer" ($command -match 'PhotoViewer\.dll') ("(" + [WlLab]::Assoc($ext, 20) + ": " + $command + ")")
}
$txt = [WlLab]::Assoc('.txt', 1)
Check '.txt opens with the classic Notepad' ($txt -match 'System32\\notepad\.exe') ("(" + [WlLab]::Assoc('.txt', 20) + ": " + $txt + ")")

Say ''
Say ("=== " + $(if ($script:fails -eq 0) { 'ALL PASSED' } else { "$script:fails FAILED" }))

# ---------------------------------------------------------------- copy onto the WLDIAG disk
try {
    $part = Get-Partition -ErrorAction Stop | Where-Object { $v = $_ | Get-Volume -ErrorAction SilentlyContinue; $v -and $v.FileSystemLabel -eq 'WLDIAG' } | Select-Object -First 1
    if ($part) {
        $at = Join-Path $base 'disk'
        New-Item -ItemType Directory -Force $at | Out-Null
        Add-PartitionAccessPath -DiskNumber $part.DiskNumber -PartitionNumber $part.PartitionNumber -AccessPath ($at + '\') -ErrorAction Stop
        Copy-Item $report $at -Force
        Copy-Item $out (Join-Path $at 'out') -Recurse -Force
        Remove-PartitionAccessPath -DiskNumber $part.DiskNumber -PartitionNumber $part.PartitionNumber -AccessPath ($at + '\')
        Write-Host 'copied onto the WLDIAG disk'
    } else { Write-Host 'no WLDIAG disk' }
} catch { Write-Host ('WLDIAG copy failed: ' + $_.Exception.Message) }

# ---------------------------------------------------------------- the menus, for the screenshots
try {
    Start-Process explorer.exe -ArgumentList "/select,`"$lab\test.txt`""
    Start-Sleep 8
    $w = New-Object -ComObject WScript.Shell
    $null = $w.AppActivate('WinLoveLab')
    Start-Sleep 1
    [WlLab]::ShiftF10()   # the classic menu of test.txt
    Start-Sleep 25
    [WlLab]::Key(0x1B)
    Start-Sleep 2
    Start-Process explorer.exe -ArgumentList "/select,`"$lab`""
    Start-Sleep 8
    $null = $w.AppActivate('Public')
    Start-Sleep 1
    [WlLab]::Key(0x5D)    # the Windows 11 menu of the folder (no "Open in Terminal")
    Start-Sleep 25
    [WlLab]::Key(0x1B)
} catch { Write-Host ('menus: ' + $_.Exception.Message) }
Start-Process notepad.exe -ArgumentList "`"$report`""
