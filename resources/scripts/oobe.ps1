# WinLove: its own welcome in place of Windows' OOBE pages (D-084, first version: the experiment).
#
# Windows' OOBE is skipped by the answer file; a setup account signs in once by itself and its first
# sign-in runs this script, full screen. Whoever installs picks the account (name, password), the
# computer's name, light or dark, an accent colour and how much Windows may collect. Then:
#   - the account is created (an administrator), the computer is renamed,
#   - the look goes into the new account's first sign-in (Default profile RunOnce: Windows writes
#     its own theme over values written earlier - ENGINE.md, D-026),
#   - the privacy policies are written and Windows' own privacy page is not shown,
#   - a start-up task deletes the setup account and its profile, the new account signs in once by
#     itself, the computer restarts.
# Reads oobe.json next to it (UTF-8): the texts, the choices, the setup account's name. "auto" in it
# (the lab): the answers are filled in and the pages go on by themselves.
#
# Colours from the design tokens (tools/gen_scripts.py replaces every @@theme.token@@).
# Keep this file ASCII: Windows PowerShell reads a script without a byte order mark as ANSI.

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$data = [System.IO.File]::ReadAllText((Join-Path $here 'oobe.json'), [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$t = $data.texts
$auto = $data.auto
$stateDir = if ($data.preview) { $here } else { Join-Path $env:ProgramData 'WinLove' } # a preview leaves nothing on this PC
New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
$logFile = Join-Path $stateDir 'oobe.log'

function Write-Log([string] $text) {
    $line = '[{0:yyyy-MM-dd HH:mm:ss}] {1}' -f (Get-Date), $text
    [System.IO.File]::AppendAllText($logFile, $line + "`r`n", [System.Text.Encoding]::UTF8)
}
Write-Log ('started as ' + [Environment]::UserName + ' on ' + $env:COMPUTERNAME)

$c = @{ bg = '@@dark.bg.base@@'; panel = '@@dark.bg.panel@@'; raised = '@@dark.bg.raised@@'; input = '@@dark.bg.input@@'
        line = '@@dark.line.subtle@@'; frame = '@@dark.line.strong@@'; text = '@@dark.text.primary@@'
        text2 = '@@dark.text.secondary@@'; text3 = '@@dark.text.tertiary@@'; accent = '@@dark.accent.base@@'
        accentHover = '@@dark.accent.hover@@'; onAccent = '@@dark.text.onAccent@@'; error = '@@dark.status.error@@' }

try {
    Add-Type -Namespace WinLove -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(System.IntPtr value);'
    [void] [WinLove.Dpi]::SetProcessDpiAwarenessContext([IntPtr] -2)
} catch { }
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase

$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="WinLove" WindowStyle="None" ResizeMode="NoResize" WindowState="Maximized" Topmost="True"
        Background="{bg}" UseLayoutRounding="True" SnapsToDevicePixels="True"
        FontFamily="Segoe UI Variable Text, Segoe UI" FontSize="13" Foreground="{text}">
  <Window.Resources>
    <Style TargetType="TextBox">
      <Setter Property="Foreground" Value="{text}"/>
      <Setter Property="CaretBrush" Value="{text}"/>
      <Setter Property="Height" Value="32"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="TextBox">
            <Border x:Name="B" Background="{input}" BorderBrush="{frame}" BorderThickness="1" CornerRadius="3" Padding="8,0">
              <ScrollViewer x:Name="PART_ContentHost" VerticalAlignment="Center"/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="B" Property="BorderBrush" Value="{accent}"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <Style TargetType="PasswordBox">
      <Setter Property="Foreground" Value="{text}"/>
      <Setter Property="CaretBrush" Value="{text}"/>
      <Setter Property="Height" Value="32"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="PasswordBox">
            <Border x:Name="B" Background="{input}" BorderBrush="{frame}" BorderThickness="1" CornerRadius="3" Padding="8,0">
              <ScrollViewer x:Name="PART_ContentHost" VerticalAlignment="Center"/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="B" Property="BorderBrush" Value="{accent}"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <Style x:Key="Button" TargetType="Button">
      <Setter Property="Foreground" Value="{text}"/>
      <Setter Property="Background" Value="{raised}"/>
      <Setter Property="Height" Value="32"/>
      <Setter Property="MinWidth" Value="96"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Border x:Name="B" Background="{TemplateBinding Background}" BorderBrush="{frame}" BorderThickness="1" CornerRadius="3" Padding="16,0">
              <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsEnabled" Value="False"><Setter TargetName="B" Property="Opacity" Value="0.45"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <Style x:Key="Primary" TargetType="Button" BasedOn="{StaticResource Button}">
      <Setter Property="Foreground" Value="{onAccent}"/>
      <Setter Property="Background" Value="{accent}"/>
    </Style>
    <Style TargetType="TextBlock" x:Key="Label">
      <Setter Property="Foreground" Value="{text2}"/>
      <Setter Property="FontSize" Value="12"/>
      <Setter Property="Margin" Value="0,14,0,6"/>
    </Style>
  </Window.Resources>
  <Grid>
    <StackPanel Width="520" HorizontalAlignment="Center" VerticalAlignment="Center">
      <StackPanel Orientation="Horizontal">
        <Path Data="M8 14L2 8V5l2-2h2l2 2 2-2h2l2 2v3z M5 8l3 3 3-3" Stroke="{accent}" StrokeThickness="1.25"
              StrokeLineJoin="Round" Width="16" Height="16"/>
        <TextBlock Text="WinLove" Margin="8,0,0,1" FontWeight="SemiBold" VerticalAlignment="Center"/>
        <TextBlock x:Name="Step" Margin="12,0,0,1" Foreground="{text3}" FontSize="12" FontFamily="Cascadia Mono, Consolas"
                   VerticalAlignment="Center"/>
      </StackPanel>
      <TextBlock x:Name="Heading" Margin="0,24,0,0" FontSize="26" FontWeight="SemiBold" TextWrapping="Wrap"/>
      <TextBlock x:Name="Sub" Margin="0,8,0,0" Foreground="{text2}" TextWrapping="Wrap"/>
      <Grid Margin="0,8,0,0" MinHeight="230">
        <StackPanel x:Name="PageAccount">
          <TextBlock x:Name="NameLabel" Style="{StaticResource Label}"/>
          <TextBox x:Name="UserName" MaxLength="20"/>
          <TextBlock x:Name="PassLabel" Style="{StaticResource Label}"/>
          <PasswordBox x:Name="Pass"/>
          <TextBlock x:Name="Pass2Label" Style="{StaticResource Label}"/>
          <PasswordBox x:Name="Pass2"/>
        </StackPanel>
        <StackPanel x:Name="PagePc" Visibility="Collapsed">
          <TextBlock x:Name="PcLabel" Style="{StaticResource Label}"/>
          <TextBox x:Name="PcName" MaxLength="15"/>
          <TextBlock x:Name="PcHint" Margin="0,8,0,0" Foreground="{text3}" FontSize="12" TextWrapping="Wrap"/>
        </StackPanel>
        <StackPanel x:Name="PageLook" Visibility="Collapsed">
          <TextBlock x:Name="ThemeLabel" Style="{StaticResource Label}"/>
          <StackPanel x:Name="Themes" Orientation="Horizontal"/>
          <TextBlock x:Name="AccentLabel" Style="{StaticResource Label}"/>
          <WrapPanel x:Name="Accents"/>
        </StackPanel>
        <StackPanel x:Name="PagePrivacy" Visibility="Collapsed">
          <StackPanel x:Name="Levels" Margin="0,14,0,0"/>
        </StackPanel>
        <StackPanel x:Name="PageDone" Visibility="Collapsed">
          <Grid Margin="0,18,0,0" Height="2" Background="{line}">
            <Border x:Name="Fill" HorizontalAlignment="Left" Width="0" Background="{accent}"/>
          </Grid>
          <TextBlock x:Name="Status" Margin="0,10,0,0" Foreground="{text2}" FontSize="12"/>
        </StackPanel>
      </Grid>
      <TextBlock x:Name="Error" Margin="0,8,0,0" Foreground="{error}" FontSize="12" TextWrapping="Wrap"/>
      <DockPanel Margin="0,20,0,0" LastChildFill="False">
        <Button x:Name="Back" Style="{StaticResource Button}" DockPanel.Dock="Left"/>
        <Button x:Name="Next" Style="{StaticResource Primary}" DockPanel.Dock="Right"/>
      </DockPanel>
    </StackPanel>
  </Grid>
</Window>
'@

$choiceXaml = @'
<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Background="{panel}" BorderBrush="{frame}"
        BorderThickness="1" CornerRadius="3" Padding="12,8" Margin="0,0,8,8" Cursor="Hand">
  <StackPanel>
    <TextBlock FontWeight="SemiBold"/>
    <TextBlock Foreground="{text2}" FontSize="12" TextWrapping="Wrap" Margin="0,2,0,0"/>
  </StackPanel>
</Border>
'@

function Expand-Xaml([string] $text) {
    foreach ($key in $c.Keys) { $text = $text.Replace('{' + $key + '}', $c[$key]) }
    return $text
}

$window = [Windows.Markup.XamlReader]::Parse((Expand-Xaml $xaml))
$names = 'Step', 'Heading', 'Sub', 'PageAccount', 'PagePc', 'PageLook', 'PagePrivacy', 'PageDone', 'NameLabel', 'UserName',
         'PassLabel', 'Pass', 'Pass2Label', 'Pass2', 'PcLabel', 'PcName', 'PcHint', 'ThemeLabel', 'Themes', 'AccentLabel',
         'Accents', 'Levels', 'Fill', 'Status', 'Error', 'Back', 'Next'
$ui = @{}
foreach ($name in $names) { $ui[$name] = $window.FindName($name) }
$brush = New-Object System.Windows.Media.BrushConverter

$ui.NameLabel.Text = $t.name
$ui.PassLabel.Text = $t.password
$ui.Pass2Label.Text = $t.password2
$ui.PcLabel.Text = $t.computer
$ui.PcHint.Text = $t.computerHint
$ui.ThemeLabel.Text = $t.theme
$ui.AccentLabel.Text = $t.accent
$ui.Back.Content = $t.back

# The finished flag lets the window close; before that Alt+F4 does nothing.
$script:finished = $false
$window.Add_Closing({ param($s, $e) if (-not $script:finished) { $e.Cancel = $true } })

# ---- choices: theme, accent, privacy ---------------------------------------------------------------
$script:theme = [string] $data.defaults.theme
$script:accent = [string] $data.defaults.accent
$script:privacy = [string] $data.defaults.privacy

function New-Choice([string] $title, [string] $detail, [double] $width) {
    $card = [Windows.Markup.XamlReader]::Parse((Expand-Xaml $choiceXaml))
    $card.Width = $width
    $card.Child.Children[0].Text = $title
    $card.Child.Children[1].Text = $detail
    if (-not $detail) { $card.Child.Children[1].Visibility = 'Collapsed' }
    return $card
}
function Show-Selected([System.Windows.Controls.Panel] $panel, [string] $value) {
    foreach ($card in $panel.Children) {
        $on = [string] $card.Tag -eq $value
        # A swatch is itself a colour: its frame is the text colour.
        $mark = if ($panel -eq $ui.Accents) { $c.text } else { $c.accent }
        $card.BorderBrush = $brush.ConvertFromString($(if ($on) { $mark } else { $c.frame }))
        $card.BorderThickness = $(if ($on) { 2 } else { 1 })
    }
}

# (Loop variables of their own: at script level $theme would be $script:theme.)
foreach ($entry in $data.themes) {
    $card = New-Choice $entry.name '' 160
    $card.Tag = $entry.id
    $card.Add_MouseLeftButtonUp({ param($s) $script:theme = [string] $s.Tag; Show-Selected $ui.Themes $script:theme })
    [void] $ui.Themes.Children.Add($card)
}
foreach ($entry in $data.accents) {
    $swatch = New-Object System.Windows.Controls.Border
    $swatch.Width = 40; $swatch.Height = 40; $swatch.CornerRadius = 3; $swatch.Margin = '0,0,8,8'; $swatch.Cursor = 'Hand'
    $swatch.Background = $brush.ConvertFromString($entry.color)
    $swatch.BorderBrush = $brush.ConvertFromString($c.frame)
    $swatch.BorderThickness = 1
    $swatch.Tag = $entry.color
    $swatch.ToolTip = $entry.name
    $swatch.Add_MouseLeftButtonUp({ param($s) $script:accent = [string] $s.Tag; Show-Selected $ui.Accents $script:accent })
    [void] $ui.Accents.Children.Add($swatch)
}
foreach ($entry in $data.privacy) {
    $card = New-Choice $entry.name $entry.detail 520
    $card.Margin = '0,0,0,8' # the column's full width: no gap on the right
    $card.Tag = $entry.id
    $card.Add_MouseLeftButtonUp({ param($s) $script:privacy = [string] $s.Tag; Show-Selected $ui.Levels $script:privacy })
    [void] $ui.Levels.Children.Add($card)
}
Show-Selected $ui.Themes $script:theme
Show-Selected $ui.Accents $script:accent
Show-Selected $ui.Levels $script:privacy

# ---- pages ------------------------------------------------------------------------------------------
# The account page always; the others as WinLove's answer file asked ("pages": ["computer", "look", "privacy"]).
$allPages = @(
    @{ id = 'account'; panel = 'PageAccount'; heading = $t.accountHeading; sub = $t.accountSub },
    @{ id = 'computer'; panel = 'PagePc'; heading = $t.pcHeading; sub = $t.pcSub },
    @{ id = 'look'; panel = 'PageLook'; heading = $t.lookHeading; sub = $t.lookSub },
    @{ id = 'privacy'; panel = 'PagePrivacy'; heading = $t.privacyHeading; sub = $t.privacySub }
)
$wanted = if ($null -ne $data.pages) { @($data.pages) } else { @('computer', 'look', 'privacy') }
$pages = @($allPages | Where-Object { $_.id -eq 'account' -or $wanted -contains $_.id })
function Test-Shown([string] $id) { return [bool] ($pages | Where-Object { $_.id -eq $id }) }
$script:page = 0

function Show-Page([int] $index) {
    $script:page = $index
    for ($i = 0; $i -lt $pages.Count; $i++) {
        $ui[$pages[$i].panel].Visibility = $(if ($i -eq $index) { 'Visible' } else { 'Collapsed' })
    }
    $ui.Heading.Text = $pages[$index].heading
    $ui.Sub.Text = $pages[$index].sub
    $ui.Step.Text = '{0} / {1}' -f ($index + 1), $pages.Count
    $ui.Back.Visibility = $(if ($index -gt 0) { 'Visible' } else { 'Hidden' })
    $ui.Next.Content = $(if ($index -eq $pages.Count - 1) { $t.finish } else { $t.next })
    $ui.Error.Text = ''
    if ($pages[$index].id -eq 'account') { [void] $ui.UserName.Focus() }
    if ($pages[$index].id -eq 'computer') {
        if (-not $ui.PcName.Text) { $ui.PcName.Text = Get-DefaultComputerName $ui.UserName.Text }
        [void] $ui.PcName.Focus()
    }
}

$reserved = @('administrator', 'guest', 'defaultaccount', 'wdagutilityaccount', 'system', 'none', ([string] $data.setupAccount).ToLower())

function Get-DefaultComputerName([string] $user) {
    if ($data.computerName) { return [string] $data.computerName } # what the answer file suggested
    $clean = ($user.ToUpper() -replace '[^A-Z0-9]', '')
    if ($clean.Length -gt 12) { $clean = $clean.Substring(0, 12) }
    if (-not $clean) { return 'PC' }
    return $clean + '-PC'
}

function Test-Page([int] $index) {
    $id = $pages[$index].id
    if ($id -eq 'account') {
        $name = $ui.UserName.Text.Trim()
        if (-not $name) { return $t.errorName }
        if ($name -match '["/\\\[\]:;|=,+*?<>@%]' -or $name.Trim('.') -eq '') { return $t.errorNameChars }
        if ($reserved -contains $name.ToLower()) { return $t.errorNameTaken }
        if (Get-LocalUser -Name $name -ErrorAction SilentlyContinue) { return $t.errorNameTaken }
        if ($ui.Pass.Password -ne $ui.Pass2.Password) { return $t.errorPasswords }
        if (-not $ui.Pass.Password -and -not $data.allowEmptyPassword) { return $t.errorPasswordEmpty }
    } elseif ($id -eq 'computer') {
        $pc = $ui.PcName.Text.Trim()
        if ($pc -notmatch '^[A-Za-z0-9-]{1,15}$' -or $pc -match '^[0-9]+$') { return $t.errorComputer }
    }
    return ''
}

$ui.Back.Add_Click({ if ($script:page -gt 0) { Show-Page ($script:page - 1) } })
$ui.Next.Add_Click({
    $problem = Test-Page $script:page
    if ($problem) { $ui.Error.Text = $problem; return }
    if ($script:page -lt $pages.Count - 1) { Show-Page ($script:page + 1) } else { $script:go = $true }
})
$window.Add_KeyDown({ param($s, $e) if ($e.Key -eq 'Return') { $ui.Next.RaiseEvent((New-Object System.Windows.RoutedEventArgs([System.Windows.Controls.Button]::ClickEvent))) } })

$script:keepTop = (Get-Date).AddMinutes(3)
$script:lastTop = Get-Date
function Update-Ui {
    # Windows' first sign-in screen and the Start menu it opens come up over a window started before
    # them: for the first minutes the window takes the top again every two seconds.
    if ((Get-Date) -lt $script:keepTop -and ((Get-Date) - $script:lastTop).TotalSeconds -ge 2 -and -not $data.preview) {
        $script:lastTop = Get-Date
        # Windows 11 opens Start at the first sign-in, and Start is above any topmost window: when
        # Start or Search has the keyboard, one Escape closes it (VM: Start covered the pages).
        try {
            $owner = Get-Process -Id ([WinLove.Desk]::ForegroundProcess()) -ErrorAction Stop
            if ($owner.ProcessName -in 'StartMenuExperienceHost', 'SearchHost', 'SearchApp', 'ShellExperienceHost') {
                [WinLove.Desk]::Escape()
                Write-Log ('closed ' + $owner.ProcessName)
            }
        } catch { }
        $window.Topmost = $false; $window.Topmost = $true
        [void] $window.Activate()
    }
    $frame = New-Object System.Windows.Threading.DispatcherFrame
    $callback = [System.Windows.Threading.DispatcherOperationCallback] { param($f) $f.Continue = $false; return $null }
    [void] [System.Windows.Threading.Dispatcher]::CurrentDispatcher.BeginInvoke([System.Windows.Threading.DispatcherPriority]::Background, $callback, $frame)
    [System.Windows.Threading.Dispatcher]::PushFrame($frame)
}
function Wait-Seconds([double] $seconds) {
    $end = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $end) { Update-Ui; Start-Sleep -Milliseconds 30 }
}

# Windows shows its own first sign-in screen ("This might take several minutes") on the Winlogon
# desktop while the account's shell starts on the Default one: no window can be above it (VM: the
# lab answers ran all pages behind it). So the window waits until the input desktop is Default.
if (-not $data.preview) {
    try {
        Add-Type -Namespace WinLove -Name Desk -UsingNamespace System.Text -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool GetUserObjectInformation(IntPtr handle, int index, StringBuilder name, int length, out int needed);
[DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr handle);
[DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
[DllImport("user32.dll")] static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
public static uint ForegroundProcess() { uint id; GetWindowThreadProcessId(GetForegroundWindow(), out id); return id; }
public static void Escape() { keybd_event(0x1B, 0, 0, UIntPtr.Zero); keybd_event(0x1B, 0, 2, UIntPtr.Zero); }
public static string Input() {
    IntPtr desk = OpenInputDesktop(0, false, 0x0001);
    if (desk == IntPtr.Zero) return "";
    var name = new StringBuilder(256); int needed;
    GetUserObjectInformation(desk, 2, name, 512, out needed);
    CloseDesktop(desk);
    return name.ToString();
}
'@
        $until = (Get-Date).AddMinutes(10)
        while ((Get-Date) -lt $until -and [WinLove.Desk]::Input() -ne 'Default') { Start-Sleep -Milliseconds 500 }
        Write-Log ('input desktop: ' + [WinLove.Desk]::Input())
    } catch { Write-Log ('input desktop unknown: ' + $_.Exception.Message) }
    # The input desktop can be Default while Windows' "This might take several minutes" screen is
    # still up (VM, 26200: the lab answers ran all pages under it): its process goes first.
    $until = (Get-Date).AddMinutes(10)
    $seen = $false
    while ((Get-Date) -lt $until -and (Get-Process -Name 'FirstLogonAnim' -ErrorAction SilentlyContinue)) { $seen = $true; Start-Sleep -Milliseconds 500 }
    Write-Log ('first sign-in screen ' + $(if ($seen) { 'gone' } else { 'not seen' }) + '; windows: ' +
               ((Get-Process | Where-Object { $_.MainWindowTitle } | ForEach-Object { $_.ProcessName }) -join ', '))
    Start-Sleep -Seconds 2
}

# "preview" (WinLove's own preview, screenshots): a window instead of the whole screen, nothing is done.
if ($data.preview) {
    $window.WindowState = 'Normal'; $window.Topmost = $false
    $window.Width = 1100; $window.Height = 680; $window.WindowStartupLocation = 'CenterScreen'
} else {
    # The whole screen, the taskbar too (a maximized window stops at the work area: VM, the taskbar showed).
    $window.WindowState = 'Normal'
    $window.Left = 0; $window.Top = 0
    $window.Width = [System.Windows.SystemParameters]::PrimaryScreenWidth
    $window.Height = [System.Windows.SystemParameters]::PrimaryScreenHeight
}
$window.Show()
[void] $window.Activate()
Show-Page ([int] $data.previewPage)

# ---- the lab: the answers fill in and the pages go on by themselves ---------------------------------
$script:go = $false
if ($auto) {
    $pause = if ($auto.pause) { [double] $auto.pause } else { 4 }
    $ui.UserName.Text = $auto.name; $ui.Pass.Password = $auto.password; $ui.Pass2.Password = $auto.password
    if ($auto.computer) { $ui.PcName.Text = $auto.computer }
    $script:theme = $auto.theme; Show-Selected $ui.Themes $script:theme
    $script:accent = $auto.accent; Show-Selected $ui.Accents $script:accent
    $script:privacy = $auto.privacy; Show-Selected $ui.Levels $script:privacy
    $problem = ''
    for ($i = 0; $i -lt $pages.Count; $i++) {
        Show-Page $i
        Wait-Seconds $pause
        if (-not $problem) { $problem = Test-Page $i }
    }
    if ($problem) { Write-Log ('auto answers refused: ' + $problem) } else { $script:go = $true }
}
while (-not $script:go) { Update-Ui; Start-Sleep -Milliseconds 30 }
if ($data.preview) { # never on the PC that previews it
    Write-Log 'preview: nothing done'
    $script:finished = $true
    $window.Close()
    return
}

# ---- doing it ---------------------------------------------------------------------------------------
$name = $ui.UserName.Text.Trim()
$password = $ui.Pass.Password
$computer = if (Test-Shown 'computer') { $ui.PcName.Text.Trim() } elseif ($data.computerName) { [string] $data.computerName } else { $env:COMPUTERNAME }
foreach ($panel in 'PageAccount', 'PagePc', 'PageLook', 'PagePrivacy') { $ui[$panel].Visibility = 'Collapsed' }
$ui.PageDone.Visibility = 'Visible'
$ui.Heading.Text = $t.doneHeading
$ui.Sub.Text = $t.doneSub
$ui.Step.Text = ''
$ui.Back.Visibility = 'Hidden'
$ui.Next.Visibility = 'Hidden'
$steps = 6
function Set-Step([int] $n, [string] $text) {
    $ui.Status.Text = $text
    $ui.Fill.Width = 520 * $n / $steps
    Update-Ui
    Write-Log $text
}

function Get-Bgr([string] $hex) { # "#RRGGBB" -> 0xAABBGGRR with alpha FF (what DWM and Explorer keep)
    $r = [Convert]::ToInt64($hex.Substring(1, 2), 16); $g = [Convert]::ToInt64($hex.Substring(3, 2), 16); $b = [Convert]::ToInt64($hex.Substring(5, 2), 16)
    # Int64 arithmetic: in PowerShell 0xFF -shl 24 is a negative Int32 and fails the UInt32 cast.
    return [uint32] (4278190080 + $b * 65536 + $g * 256 + $r)
}
function Get-Mix([string] $hex, [int] $toward, [double] $amount) { # toward 255 (lighter) or 0 (darker)
    $out = @()
    foreach ($i in 1, 3, 5) {
        $v = [Convert]::ToInt32($hex.Substring($i, 2), 16)
        $out += [int] [Math]::Round($v + ($toward - $v) * $amount)
    }
    return $out
}

try {
    # 1. The account.
    Set-Step 1 $t.stepAccount
    if ($password) {
        $secure = ConvertTo-SecureString $password -AsPlainText -Force
        New-LocalUser -Name $name -FullName $name -Password $secure -PasswordNeverExpires -AccountNeverExpires -ErrorAction Stop | Out-Null
    } else {
        New-LocalUser -Name $name -FullName $name -NoPassword -AccountNeverExpires -ErrorAction Stop | Out-Null
    }
    Add-LocalGroupMember -SID 'S-1-5-32-544' -Member $name -ErrorAction Stop   # Administrators, in any language
    Write-Log "account $name created"

    # 2. The computer's name (from the next start).
    Set-Step 2 $t.stepComputer
    if ($computer -ne $env:COMPUTERNAME) { Rename-Computer -NewName $computer -Force -ErrorAction Stop -WarningAction SilentlyContinue }

    # 3. Privacy: machine policies; Windows' own privacy page is not shown to the new account.
    Set-Step 3 $t.stepPrivacy
    $oobe = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\OOBE'
    New-Item -Path $oobe -Force | Out-Null
    Set-ItemProperty -Path $oobe -Name DisablePrivacyExperience -Value 1 -Type DWord
    $level = $data.privacy | Where-Object { $_.id -eq $script:privacy } | Select-Object -First 1
    foreach ($write in @($level.writes)) {
        if (-not $write) { continue }
        New-Item -Path $write.key -Force | Out-Null
        Set-ItemProperty -Path $write.key -Name $write.name -Value $write.value -Type DWord
    }

    # 4. The look, at the new account's first sign-in (a .reg the Default profile's RunOnce imports).
    Set-Step 4 $t.stepLook
    if (Test-Shown 'look') { # without the page Windows' own look stays
        $light = [int] ($script:theme -eq 'light')
        $base = $script:accent
        $shades = @((Get-Mix $base 255 0.65), (Get-Mix $base 255 0.45), (Get-Mix $base 255 0.2), (Get-Mix $base 0 0.0),
                    (Get-Mix $base 0 0.15), (Get-Mix $base 0 0.4), (Get-Mix $base 0 0.65), @(0xF7, 0x63, 0x0C))
        $palette = ($shades | ForEach-Object { '{0:x2},{1:x2},{2:x2},00' -f $_[0], $_[1], $_[2] }) -join ','
        $dark2 = '#{0:X2}{1:X2}{2:X2}' -f $shades[5][0], $shades[5][1], $shades[5][2]
        $argb = [uint32] (3288334336 + [Convert]::ToInt64($base.Substring(1), 16)) # alpha C4, then RGB
        $reg = @(
            'Windows Registry Editor Version 5.00', '',
            '[HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize]',
            ('"AppsUseLightTheme"=dword:{0:x8}' -f $light), ('"SystemUsesLightTheme"=dword:{0:x8}' -f $light), '',
            '[HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\Accent]',
            ('"AccentPalette"=hex:' + $palette), ('"AccentColorMenu"=dword:{0:x8}' -f (Get-Bgr $base)),
            ('"StartColorMenu"=dword:{0:x8}' -f (Get-Bgr $dark2)), '',
            '[HKEY_CURRENT_USER\Software\Microsoft\Windows\DWM]',
            ('"AccentColor"=dword:{0:x8}' -f (Get-Bgr $base)), ('"ColorizationColor"=dword:{0:x8}' -f $argb),
            ('"ColorizationAfterglow"=dword:{0:x8}' -f $argb), '"EnableWindowColorization"=dword:00000000', '',
            '[HKEY_CURRENT_USER\Control Panel\Desktop]', '"AutoColorization"="0"', '')
        $regFile = Join-Path $stateDir 'oobe-user.reg'
        [System.IO.File]::WriteAllText($regFile, ($reg -join "`r`n"), [System.Text.Encoding]::Unicode)
        & reg.exe load 'HKU\WinLoveOobe' "$env:SystemDrive\Users\Default\NTUSER.DAT" | Out-Null
        & reg.exe add 'HKU\WinLoveOobe\Software\Microsoft\Windows\CurrentVersion\RunOnce' /v WinLoveOobe /t REG_SZ /d "reg import `"$regFile`"" /f | Out-Null
        [gc]::Collect()
        & reg.exe unload 'HKU\WinLoveOobe' | Out-Null
    }

    # 5. The setup account goes at the next start (its profile is free then).
    Set-Step 5 $t.stepCleanup
    $setup = [string] $data.setupAccount
    if ($setup -and $setup -eq [Environment]::UserName) {
        $cleanup = Join-Path $stateDir 'oobe-cleanup.ps1'
        $lines = @(
            ('$name = ''{0}''' -f $setup),
            'Get-CimInstance Win32_UserProfile | Where-Object { $_.LocalPath -like ("*\" + $name) } | Remove-CimInstance',
            'Remove-LocalUser -Name $name -ErrorAction SilentlyContinue',
            ('Remove-Item -LiteralPath ''{0}'' -Force -ErrorAction SilentlyContinue' -f (Join-Path $here 'oobe.json')),
            'Unregister-ScheduledTask -TaskName ''WinLove OOBE cleanup'' -Confirm:$false',
            'Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force')
        [System.IO.File]::WriteAllText($cleanup, ($lines -join "`r`n"), [System.Text.Encoding]::UTF8)
        $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$cleanup`""
        $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
        Register-ScheduledTask -TaskName 'WinLove OOBE cleanup' -Action $action -Trigger (New-ScheduledTaskTrigger -AtStartup) -Principal $principal -Force | Out-Null
    }
    if ($auto -and $auto.check) {
        # The lab: what the new account finds at its first sign-in, onto the log disk.
        $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument ("-NoProfile -ExecutionPolicy Bypass -File `"{0}`"" -f (Join-Path $here 'oobe-check.ps1'))
        $principal = New-ScheduledTaskPrincipal -UserId $name -LogonType Interactive -RunLevel Highest
        Register-ScheduledTask -TaskName 'WinLove OOBE check' -Action $action -Trigger (New-ScheduledTaskTrigger -AtLogOn -User $name) -Principal $principal -Force | Out-Null
    }

    # 6. The new account signs in once by itself. Windows does NOT forget the password afterwards
    #    (VM, 26200: AutoLogonCount 0, DefaultPassword still there in plain text): a task at that
    #    sign-in, as SYSTEM, takes it out and goes.
    Set-Step 6 $t.stepRestart
    $signin = Join-Path $stateDir 'oobe-signin.ps1'
    $lines = @(
        '$winlogon = ''HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon''',
        'Remove-ItemProperty -Path $winlogon -Name DefaultPassword -ErrorAction SilentlyContinue',
        'Remove-ItemProperty -Path $winlogon -Name AutoLogonCount -ErrorAction SilentlyContinue',
        'Set-ItemProperty -Path $winlogon -Name AutoAdminLogon -Value ''0''',
        'Unregister-ScheduledTask -TaskName ''WinLove OOBE sign-in'' -Confirm:$false',
        'Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force')
    [System.IO.File]::WriteAllText($signin, ($lines -join "`r`n"), [System.Text.Encoding]::UTF8)
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$signin`""
    $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
    Register-ScheduledTask -TaskName 'WinLove OOBE sign-in' -Action $action -Trigger (New-ScheduledTaskTrigger -AtLogOn -User $name) -Principal $principal -Force | Out-Null
    $winlogon = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
    Set-ItemProperty -Path $winlogon -Name AutoAdminLogon -Value '1'
    Set-ItemProperty -Path $winlogon -Name DefaultUserName -Value $name
    Set-ItemProperty -Path $winlogon -Name DefaultDomainName -Value $computer
    Set-ItemProperty -Path $winlogon -Name DefaultPassword -Value $password
    Set-ItemProperty -Path $winlogon -Name AutoLogonCount -Value 1 -Type DWord
    Write-Log 'done; restarting'
    Wait-Seconds 2
    $script:finished = $true
    Restart-Computer -Force
} catch {
    Write-Log ('FAILED: ' + $_.Exception.Message)
    $ui.Error.Text = $t.failed + ' ' + $_.Exception.Message
    $script:finished = $true
    Wait-Seconds 30
}
