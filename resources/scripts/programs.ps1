# WinLove: installs the programs chosen on WinLove's Programs page with winget, at the first
# sign-in, in a small window that shows each program as it goes (D-078).
#
# Started by the "WinLove Programs" task: the user who signs in, elevated when the account is an
# administrator. Reads programs.json next to it (UTF-8: the names and the window's texts).
# Without a network connection it waits on screen until there is one. A program that is in
# place is written to %ProgramData%\WinLove\programs-done.txt; when the window is closed or the
# session ends before the end, the task stays and the next sign-in goes on with the rest.
# When every program is installed or has failed, the task removes itself.
# "dryRun": true in programs.json: the window as it would go, nothing installed (no winget call,
# no task removed, the state file under %TEMP%) - a preview for the lab and for WinLove itself;
# "dryRunOffline": true adds a few seconds without a network.
#
# The window follows WinLove's splash (00_brand): bg.base, a 1px line.strong frame, the mark,
# a 2px progress line, no shadow. Its colours come from the design tokens: tools/gen_scripts.py
# replaces every @@theme.token@@ with the value in WinLove-UI-Handoff/01_tokens/tokens.json.
# Light or dark as Windows' app theme is.
#
# Keep this file ASCII: Windows PowerShell reads a script without a byte order mark as ANSI.

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$data = [System.IO.File]::ReadAllText((Join-Path $here 'programs.json'), [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$texts = $data.texts
$taskName = 'WinLove Programs'
$stateDir = Join-Path $env:ProgramData 'WinLove'
New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
$logFile = Join-Path $stateDir 'programs.log'
$doneFile = Join-Path $stateDir 'programs-done.txt'
$dryRun = [bool] $data.dryRun
if ($dryRun) {
    $logFile = Join-Path $env:TEMP 'winlove-programs-preview.log'
    $doneFile = Join-Path $env:TEMP 'winlove-programs-preview-done.txt'
    Remove-Item $doneFile -ErrorAction SilentlyContinue
}

function Write-Log([string] $text) {
    $line = '[{0:yyyy-MM-dd HH:mm:ss}] {1}' -f (Get-Date), $text
    [System.IO.File]::AppendAllText($logFile, $line + "`r`n", [System.Text.Encoding]::UTF8)
}

function Format-Text([string] $template, [hashtable] $values) {
    $out = $template
    foreach ($key in $values.Keys) { $out = $out.Replace('{' + $key + '}', [string] $values[$key]) }
    return $out
}

Write-Log ('started as ' + [Environment]::UserName + ', ' + @($data.programs).Count + ' program(s)')
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

# ---- colours: Windows' app theme picks the token set ---------------------------------------------
$light = $false
try {
    $light = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize' -ErrorAction Stop).AppsUseLightTheme -eq 1
} catch { }
if ($light) {
    $c = @{ bg = '@@light.bg.base@@'; panel = '@@light.bg.panel@@'; raised = '@@light.bg.raised@@'; line = '@@light.line.subtle@@'
            frame = '@@light.line.strong@@'; text = '@@light.text.primary@@'; text2 = '@@light.text.secondary@@'
            text3 = '@@light.text.tertiary@@'; accent = '@@light.accent.base@@'; accentHover = '@@light.accent.hover@@'
            onAccent = '@@light.text.onAccent@@'; success = '@@light.status.success@@'; error = '@@light.status.error@@'
            warning = '@@light.status.warning@@'; warningSubtle = '@@light.status.warningSubtle@@' }
} else {
    $c = @{ bg = '@@dark.bg.base@@'; panel = '@@dark.bg.panel@@'; raised = '@@dark.bg.raised@@'; line = '@@dark.line.subtle@@'
            frame = '@@dark.line.strong@@'; text = '@@dark.text.primary@@'; text2 = '@@dark.text.secondary@@'
            text3 = '@@dark.text.tertiary@@'; accent = '@@dark.accent.base@@'; accentHover = '@@dark.accent.hover@@'
            onAccent = '@@dark.text.onAccent@@'; success = '@@dark.status.success@@'; error = '@@dark.status.error@@'
            warning = '@@dark.status.warning@@'; warningSubtle = '@@dark.status.warningSubtle@@' }
}

# ---- the window -----------------------------------------------------------------------------------
try {
    # System DPI aware before WPF starts: crisp at 125 / 150 / 175 %.
    Add-Type -Namespace WinLove -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(System.IntPtr value);'
    [void] [WinLove.Dpi]::SetProcessDpiAwarenessContext([IntPtr] -2)
} catch { }
Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase

$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="WinLove" Width="520" SizeToContent="Height" WindowStyle="None" ResizeMode="NoResize"
        WindowStartupLocation="CenterScreen" Background="{bg}" UseLayoutRounding="True" SnapsToDevicePixels="True"
        FontFamily="Segoe UI Variable Text, Segoe UI" FontSize="13" Foreground="{text}">
  <Window.Resources>
    <Style x:Key="Primary" TargetType="Button">
      <Setter Property="Foreground" Value="{onAccent}"/>
      <Setter Property="Background" Value="{accent}"/>
      <Setter Property="FontSize" Value="12"/>
      <Setter Property="Cursor" Value="Hand"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Border x:Name="B" Background="{TemplateBinding Background}" CornerRadius="3" Padding="16,0">
              <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="B" Property="Background" Value="{accentHover}"/></Trigger>
              <Trigger Property="IsEnabled" Value="False"><Setter TargetName="B" Property="Opacity" Value="0.45"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <Style x:Key="Caption" TargetType="Button">
      <Setter Property="Foreground" Value="{text2}"/>
      <Setter Property="Background" Value="Transparent"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="Button">
            <Border x:Name="B" Background="{TemplateBinding Background}">
              <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
            </Border>
            <ControlTemplate.Triggers>
              <Trigger Property="IsMouseOver" Value="True"><Setter TargetName="B" Property="Background" Value="{raised}"/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <!-- WinLove's own scroll bar: a 4 px thumb, 8 px under the mouse, no arrows (WPF's is always light). -->
    <Style x:Key="PageClick" TargetType="RepeatButton">
      <Setter Property="Focusable" Value="False"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="RepeatButton"><Border Background="Transparent"/></ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
    <Style TargetType="ScrollBar">
      <Setter Property="Width" Value="10"/>
      <Setter Property="MinWidth" Value="10"/>
      <Setter Property="Template">
        <Setter.Value>
          <ControlTemplate TargetType="ScrollBar">
            <Grid Background="Transparent">
            <Track x:Name="PART_Track" IsDirectionReversed="True">
              <Track.DecreaseRepeatButton>
                <RepeatButton Style="{StaticResource PageClick}" Command="ScrollBar.PageUpCommand"/>
              </Track.DecreaseRepeatButton>
              <Track.IncreaseRepeatButton>
                <RepeatButton Style="{StaticResource PageClick}" Command="ScrollBar.PageDownCommand"/>
              </Track.IncreaseRepeatButton>
              <Track.Thumb>
                <Thumb>
                  <Thumb.Template>
                    <ControlTemplate TargetType="Thumb">
                      <Border Background="Transparent">
                        <Border x:Name="T" Width="4" HorizontalAlignment="Right" CornerRadius="2" Background="{frame}"/>
                      </Border>
                      <ControlTemplate.Triggers>
                        <DataTrigger Binding="{Binding IsMouseOver, RelativeSource={RelativeSource AncestorType=ScrollBar}}" Value="True">
                          <Setter TargetName="T" Property="Width" Value="8"/>
                          <Setter TargetName="T" Property="Background" Value="{text3}"/>
                        </DataTrigger>
                      </ControlTemplate.Triggers>
                    </ControlTemplate>
                  </Thumb.Template>
                </Thumb>
              </Track.Thumb>
            </Track>
            </Grid>
          </ControlTemplate>
        </Setter.Value>
      </Setter>
    </Style>
  </Window.Resources>
  <Border BorderBrush="{frame}" BorderThickness="1">
    <DockPanel>
      <Grid x:Name="TitleBar" DockPanel.Dock="Top" Height="36" Background="{bg}">
        <StackPanel Orientation="Horizontal" Margin="14,0,0,0" VerticalAlignment="Center">
          <Path Data="M8 14L2 8V5l2-2h2l2 2 2-2h2l2 2v3z M5 8l3 3 3-3" Stroke="{accent}" StrokeThickness="1.25"
                StrokeLineJoin="Round" Width="16" Height="16"/>
          <TextBlock Text="WinLove" Margin="8,0,0,1" FontWeight="SemiBold" VerticalAlignment="Center"/>
        </StackPanel>
        <Button x:Name="CloseX" Style="{StaticResource Caption}" HorizontalAlignment="Right" Width="44"
                FontFamily="Segoe Fluent Icons, Segoe MDL2 Assets" FontSize="10" Content="&#xE8BB;"/>
      </Grid>
      <Grid DockPanel.Dock="Bottom" Height="52" Background="{panel}">
        <Border BorderBrush="{line}" BorderThickness="0,1,0,0"/>
        <TextBlock x:Name="Hint" Margin="20,0,150,0" VerticalAlignment="Center" Foreground="{text3}" FontSize="12"
                   TextWrapping="Wrap"/>
        <Button x:Name="Done" Style="{StaticResource Primary}" HorizontalAlignment="Right" Margin="0,0,20,0" Height="28"
                MinWidth="88" IsEnabled="False"/>
      </Grid>
      <StackPanel Margin="24,10,24,16">
        <TextBlock x:Name="Heading" FontSize="20" FontWeight="SemiBold"/>
        <Grid Margin="0,6,0,0">
          <TextBlock x:Name="Status" Foreground="{text2}" FontSize="12" TextTrimming="CharacterEllipsis" Margin="0,0,64,0"/>
          <TextBlock x:Name="Count" HorizontalAlignment="Right" Foreground="{text3}" FontSize="12"
                     FontFamily="Cascadia Mono, Consolas"/>
        </Grid>
        <Grid x:Name="Track" Margin="0,12,0,0" Height="2" Background="{line}">
          <Border x:Name="Fill" HorizontalAlignment="Left" Width="0" Background="{accent}"/>
        </Grid>
        <Border x:Name="Banner" Margin="0,14,0,0" Padding="12,8" Background="{warningSubtle}" CornerRadius="3"
                Visibility="Collapsed">
          <DockPanel>
            <TextBlock DockPanel.Dock="Left" FontFamily="Segoe Fluent Icons, Segoe MDL2 Assets" FontSize="14"
                       Foreground="{warning}" Text="&#xE7BA;" Margin="0,1,10,0"/>
            <TextBlock x:Name="BannerText" TextWrapping="Wrap" FontSize="12"/>
          </DockPanel>
        </Border>
        <ScrollViewer Margin="0,12,0,0" MaxHeight="352" VerticalScrollBarVisibility="Auto" HorizontalScrollBarVisibility="Disabled">
          <StackPanel x:Name="Rows"/>
        </ScrollViewer>
      </StackPanel>
    </DockPanel>
  </Border>
</Window>
'@

$rowXaml = @'
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Height="32">
  <Grid.ColumnDefinitions>
    <ColumnDefinition Width="28"/>
    <ColumnDefinition Width="*"/>
    <ColumnDefinition Width="Auto"/>
  </Grid.ColumnDefinitions>
  <Border Grid.ColumnSpan="3" Background="{panel}" CornerRadius="3" Visibility="Hidden" Margin="-8,0"/>
  <TextBlock Grid.Column="0" FontFamily="Segoe Fluent Icons, Segoe MDL2 Assets" FontSize="12" VerticalAlignment="Center"
             Foreground="{text3}"/>
  <Path Grid.Column="0" Width="14" Height="14" HorizontalAlignment="Left" Stroke="{accent}" StrokeThickness="1.5"
        StrokeStartLineCap="Round" StrokeEndLineCap="Round" Data="M7 1 A6 6 0 1 1 1 7" Visibility="Collapsed"
        RenderTransformOrigin="0.5,0.5">
    <Path.RenderTransform><RotateTransform/></Path.RenderTransform>
  </Path>
  <TextBlock Grid.Column="1" VerticalAlignment="Center" TextTrimming="CharacterEllipsis"/>
  <TextBlock Grid.Column="2" VerticalAlignment="Center" FontSize="12" Foreground="{text2}" Margin="12,0,0,0"/>
</Grid>
'@

function Expand-Xaml([string] $text) {
    foreach ($key in $c.Keys) { $text = $text.Replace('{' + $key + '}', $c[$key]) }
    return $text
}

$window = [Windows.Markup.XamlReader]::Parse((Expand-Xaml $xaml))
# The taskbar shows the window's icon: WinLove's mark, drawn, not PowerShell's.
$mark = New-Object System.Windows.Media.GeometryDrawing
$mark.Geometry = [System.Windows.Media.Geometry]::Parse('M8 14L2 8V5l2-2h2l2 2 2-2h2l2 2v3z M5 8l3 3 3-3')
$mark.Pen = New-Object System.Windows.Media.Pen((New-Object System.Windows.Media.BrushConverter).ConvertFromString($c.accent), 1.4)
$mark.Pen.LineJoin = [System.Windows.Media.PenLineJoin]::Round
$window.Icon = New-Object System.Windows.Media.DrawingImage($mark)
$names = 'TitleBar', 'CloseX', 'Hint', 'Done', 'Heading', 'Status', 'Count', 'Track', 'Fill', 'Banner', 'BannerText', 'Rows'
$ui = @{}
foreach ($name in $names) { $ui[$name] = $window.FindName($name) }

$script:closed = $false
$window.Add_Closed({ $script:closed = $true })
$ui.TitleBar.Add_MouseLeftButtonDown({ $window.DragMove() })
$ui.CloseX.Add_Click({ $window.Close() })
$ui.Done.Add_Click({ $window.Close() })
$ui.Done.Content = $texts.close
$ui.Heading.Text = $texts.heading
$ui.Hint.Text = $texts.hint
$brush = New-Object System.Windows.Media.BrushConverter
$programs = @($data.programs)
$total = $programs.Count

$rows = @{}
foreach ($program in $programs) {
    $row = [Windows.Markup.XamlReader]::Parse((Expand-Xaml $rowXaml))
    $row.Children[4].Text = $texts.pending
    $row.Children[3].Text = $program.name
    $row.Children[1].Text = [string] [char] 0xEA3A   # a ring: waiting
    [void] $ui.Rows.Children.Add($row)
    $rows[$program.id] = $row
}

$window.Show()
[void] $window.Activate()

function Update-Ui {
    # WPF's DoEvents: run what the dispatcher has queued (layout, render, animation ticks).
    $frame = New-Object System.Windows.Threading.DispatcherFrame
    $callback = [System.Windows.Threading.DispatcherOperationCallback] { param($f) $f.Continue = $false; return $null }
    [void] [System.Windows.Threading.Dispatcher]::CurrentDispatcher.BeginInvoke([System.Windows.Threading.DispatcherPriority]::Background, $callback, $frame)
    [System.Windows.Threading.Dispatcher]::PushFrame($frame)
}

function Wait-Seconds([double] $seconds) {
    $end = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $end -and -not $script:closed) { Update-Ui; Start-Sleep -Milliseconds 30 }
}

function Set-Status([string] $text) { $ui.Status.Text = $text; Update-Ui }

function Set-Progress([int] $done) {
    $ui.Count.Text = '{0} / {1}' -f $done, $total
    $width = $ui.Track.ActualWidth
    if ($width -le 0) { $width = 470 }
    $ui.Fill.Width = [Math]::Max(0, $width * $done / [Math]::Max(1, $total))
    Update-Ui
}

function Show-Banner([string] $text) {
    if ($text) { $ui.BannerText.Text = $text; $ui.Banner.Visibility = 'Visible' } else { $ui.Banner.Visibility = 'Collapsed' }
    Update-Ui
}

# state: pending | installing | installed | already | failed
function Set-Row([string] $id, [string] $state, [string] $text) {
    $row = $rows[$id]
    if (-not $row) { return }
    $highlight = $row.Children[0]
    $glyph = $row.Children[1]
    $spinner = $row.Children[2]
    $label = $row.Children[4]
    $label.Text = $text
    $label.Foreground = $brush.ConvertFromString($c.text2)
    $highlight.Visibility = 'Hidden'
    $glyph.Visibility = 'Visible'
    $spinner.Visibility = 'Collapsed'
    $spinner.RenderTransform.BeginAnimation([System.Windows.Media.RotateTransform]::AngleProperty, $null)
    switch ($state) {
        'installing' {
            $highlight.Visibility = 'Visible'
            $glyph.Visibility = 'Collapsed'
            $spinner.Visibility = 'Visible'
            $turn = New-Object System.Windows.Media.Animation.DoubleAnimation(0, 360, (New-Object System.Windows.Duration([TimeSpan]::FromSeconds(0.9))))
            $turn.RepeatBehavior = [System.Windows.Media.Animation.RepeatBehavior]::Forever
            $spinner.RenderTransform.BeginAnimation([System.Windows.Media.RotateTransform]::AngleProperty, $turn)
            $label.Foreground = $brush.ConvertFromString($c.accent)
            $row.BringIntoView()
        }
        'installed' { $glyph.Text = [string] [char] 0xE73E; $glyph.Foreground = $brush.ConvertFromString($c.success) }
        'already' { $glyph.Text = [string] [char] 0xE73E; $glyph.Foreground = $brush.ConvertFromString($c.text3) }
        'failed' {
            $glyph.Text = [string] [char] 0xE711
            $glyph.Foreground = $brush.ConvertFromString($c.error)
            $label.Foreground = $brush.ConvertFromString($c.error)
        }
        default { $glyph.Text = [string] [char] 0xEA3A; $glyph.Foreground = $brush.ConvertFromString($c.text3) }
    }
    Update-Ui
}

function Stop-Here([int] $code) {
    # The window was closed (or the sign-in ends) before the end: the task stays for the next sign-in.
    Write-Log ('stopped before the end, exit ' + $code)
    exit $code
}

Set-Progress 0

# ---- the post-setup steps first (their winget steps must not run at the same time) ----------------
while (-not $script:closed) {
    $postSetup = Get-ScheduledTask -TaskName 'WinLove Post-Setup' -ErrorAction SilentlyContinue
    if (-not $postSetup -or $postSetup.State -ne 'Running') { break }
    Set-Status $texts.waitingPostSetup
    Wait-Seconds 3
}
if ($script:closed) { Stop-Here 2 }

# ---- winget: it is registered for a new user a little after the sign-in ---------------------------
function Find-Winget {
    $command = Get-Command winget.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $alias = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\winget.exe'
    if (Test-Path $alias) { return $alias }
    return $null
}

# D-104: when every program carries its own local installer, winget is not needed at all.
$allOffline = ($programs.Count -gt 0) -and -not (@($programs | Where-Object { -not $_.offline }).Count)
$winget = if ($dryRun) { 'winget.exe' } elseif ($allOffline) { $null } else { Find-Winget }
if (-not $winget -and -not $allOffline) {
    Set-Status $texts.waitingWinget
    try {
        Add-AppxPackage -RegisterByFamilyName -MainPackage Microsoft.DesktopAppInstaller_8wekyb3d8bbwe -ErrorAction Stop
        Write-Log 'App Installer registered for this user'
    } catch { Write-Log ('App Installer not registered: ' + $_.Exception.Message) }
    $deadline = (Get-Date).AddMinutes(10)
    while (-not $winget -and (Get-Date) -lt $deadline -and -not $script:closed) {
        Wait-Seconds 5
        $winget = Find-Winget
    }
}
if ($script:closed) { Stop-Here 2 }
if (-not $winget -and -not $allOffline) {
    Write-Log 'winget is not available: App Installer is missing from this Windows'
    foreach ($program in $programs) { Set-Row $program.id 'failed' $texts.notInstalled }
    Set-Status $texts.noWinget
    $ui.Done.IsEnabled = $true
    if (-not $dryRun) { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue }
    while (-not $script:closed) { Wait-Seconds 0.25 }
    exit 1
}
Write-Log ('winget: ' + $winget)

# ---- the network: wait on screen until there is one -----------------------------------------------
function Test-Online {
    if ($dryRun -and $data.dryRunOffline -and -not $script:wentOnline) {
        # The preview of the wait: offline for a few seconds.
        if (-not $script:offlineSince) { $script:offlineSince = Get-Date }
        if (((Get-Date) - $script:offlineSince).TotalSeconds -lt 6) { return $false }
        $script:wentOnline = $true
    }
    try {
        $request = [System.Net.WebRequest]::Create('https://cdn.winget.microsoft.com/cache/source2.msix')
        $request.Method = 'HEAD'
        $request.Timeout = 8000
        $response = $request.GetResponse()
        $response.Close()
        return $true
    } catch { return $false }
}

function Wait-Online {
    $logged = $false
    while (-not (Test-Online)) {
        if ($script:closed) { Stop-Here 2 }
        if (-not $logged) { Write-Log 'no network: waiting'; $logged = $true }
        Set-Status $texts.waitingNetwork
        Show-Banner $texts.waitingNetworkHint
        Wait-Seconds 3
    }
    Show-Banner ''
    if ($logged) { Write-Log 'network is back' }
}

Wait-Online
Set-Status $texts.updatingSources
if ($dryRun) {
    Wait-Seconds 2
} else {
    $sourceUpdate = Start-Process -FilePath $winget -ArgumentList @('source', 'update', '--disable-interactivity') -WindowStyle Hidden -PassThru
    $null = $sourceUpdate.Handle
    while (-not $sourceUpdate.HasExited -and -not $script:closed) { Update-Ui; Start-Sleep -Milliseconds 30 }
}
if ($script:closed) { Stop-Here 2 }

# ---- the programs ---------------------------------------------------------------------------------
$done = @{}
if (Test-Path $doneFile) { foreach ($line in [System.IO.File]::ReadAllLines($doneFile)) { if ($line) { $done[$line] = $true } } }

# winget's exit codes that mean "it is there"
$alreadyThere = @(-1978335189, -1978335135)   # 0x8A15002B no newer version, 0x8A150061 already installed
$needsRestart = @(-1978334967, 3010, 1641)    # 0x8A150109 restart to finish, MSI 3010 / 1641
$refusesAdmin = -1978335146                   # 0x8A150056 the installer cannot run as administrator (Spotify)

# The same winget command as the signed-in user WITHOUT elevation: a task of this user with the
# limited run level (no password, no UAC prompt). Its exit code comes back through a file.
function Invoke-Unelevated([string[]] $arguments, [string] $output) {
    $codeFile = $output + '.code'
    Remove-Item $output, $codeFile -ErrorAction SilentlyContinue
    $line = '"' + $winget + '" ' + ($arguments -join ' ') + ' > "' + $output + '" 2>&1 & echo !errorlevel! > "' + $codeFile + '"'
    $userTask = $taskName + ' (user)'
    try {
        $action = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument ('/v:on /s /c "' + $line + '"')
        $principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Hours 1)
        Register-ScheduledTask -TaskName $userTask -Action $action -Principal $principal -Settings $settings -Force | Out-Null
        Start-ScheduledTask -TaskName $userTask
    } catch {
        Write-Log ('unelevated run not possible: ' + $_.Exception.Message)
        return $refusesAdmin
    }
    $until = (Get-Date).AddMinutes(45)
    while (-not (Test-Path $codeFile) -and (Get-Date) -lt $until) {
        if ($script:closed) { Stop-Here 2 }
        Update-Ui
        Start-Sleep -Milliseconds 30
    }
    Start-Sleep -Milliseconds 300   # the echo's file is closed
    Unregister-ScheduledTask -TaskName $userTask -Confirm:$false -ErrorAction SilentlyContinue
    $text = if (Test-Path $codeFile) { ((Get-Content $codeFile -ErrorAction SilentlyContinue) -join '').Trim() } else { '' }
    $value = 0
    if ([int]::TryParse($text, [ref] $value)) { return $value }
    return $refusesAdmin
}
$ok = 0
$failed = 0
$index = 0
foreach ($program in $programs) {
    $index++
    if ($done.ContainsKey($program.id)) {
        Set-Row $program.id 'installed' $texts.installed
        $ok++
        Set-Progress $index
        continue
    }
    $attempt = 0
    $state = $null
    $result = $null
    while ($null -eq $state) {
        $attempt++
        Set-Status (Format-Text $texts.installing @{ name = $program.name; n = $index; total = $total })
        Set-Row $program.id 'installing' $texts.installingRow
        $output = Join-Path $env:TEMP ('winlove-winget-' + $index + '.txt')
        $arguments = @('install', '--id', $program.id, '--exact', '--silent', '--source', 'winget',
                       '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity')
        if ($program.offline -and -not $dryRun) {
            # D-104: install from the local installer embedded in the image (no internet, no winget).
            $appsFile = Join-Path $here ('apps\' + $program.id + '\' + [string] $program.offline.file)
            if (-not (Test-Path $appsFile)) {
                Write-Log ($program.id + ': offline installer missing: ' + $appsFile)
                $code = 1
            } else {
                $line = ([string] $program.offline.command).Replace('{path}', $appsFile)
                Write-Log ($program.id + ' offline: ' + $line)
                $proc = Start-Process -FilePath 'cmd.exe' -ArgumentList ('/d /c ' + $line) -WindowStyle Hidden -PassThru
                $null = $proc.Handle
                while (-not $proc.HasExited) { if ($script:closed) { Stop-Here 2 }; Update-Ui; Start-Sleep -Milliseconds 30 }
                $code = $proc.ExitCode
            }
        } elseif ($dryRun) {
            # The preview: the second program "is there already", the last one fails once, then works.
            Wait-Seconds 2.5
            if ($script:closed) { Stop-Here 2 }
            $code = if ($index -eq 2) { -1978335135 } elseif ($index -eq $total -and $attempt -eq 1) { -1978335226 } else { 0 }
        } else {
            $process = Start-Process -FilePath $winget -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $output
            $null = $process.Handle
            while (-not $process.HasExited) {
                if ($script:closed) { Stop-Here 2 }   # winget goes on; the next sign-in sees it in place
                Update-Ui
                Start-Sleep -Milliseconds 30
            }
            $code = $process.ExitCode
            if ($code -eq $refusesAdmin) {
                Write-Log ('{0}: the installer refuses an administrator; running it as {1} without elevation' -f $program.id, $env:USERNAME)
                $code = Invoke-Unelevated $arguments $output
            }
        }
        $tail = ''
        if (Test-Path $output) { $tail = ((Get-Content $output -Tail 3 -ErrorAction SilentlyContinue) -join ' ').Trim() }
        Write-Log ('{0} attempt {1}: exit {2} {3}' -f $program.id, $attempt, $code, $tail)
        if ($code -eq 0) {
            $state = 'installed'; $result = $texts.installed
        } elseif ($alreadyThere -contains $code) {
            $state = 'already'; $result = $texts.already
        } elseif ($needsRestart -contains $code) {
            $state = 'installed'; $result = $texts.restart
        } elseif (-not (Test-Online)) {
            Wait-Online                 # the connection went away: wait, then try again
        } elseif ($attempt -ge 2) {
            $state = 'failed'; $result = Format-Text $texts.failed @{ code = ('0x{0:X8}' -f $code) }
        }
    }
    if ($state -eq 'failed') {
        $failed++
    } else {
        $ok++
        [System.IO.File]::AppendAllText($doneFile, $program.id + "`r`n")
    }
    Set-Row $program.id $state $result
    Set-Progress $index
}

# ---- the end --------------------------------------------------------------------------------------
$summary = if ($failed -gt 0) { Format-Text $texts.doneWithErrors @{ ok = $ok; failed = $failed } } else { Format-Text $texts.done @{ ok = $ok } }
Write-Log $summary
$ui.Heading.Text = $texts.finished
$ui.Hint.Text = ''
Set-Status $summary
$ui.Done.IsEnabled = $true
[void] $ui.Done.Focus()
if (-not $dryRun) { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue }
Remove-Item $doneFile -ErrorAction SilentlyContinue
while (-not $script:closed) { Wait-Seconds 0.25 }
exit 0
