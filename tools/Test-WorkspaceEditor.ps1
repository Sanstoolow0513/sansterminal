# Copyright (c) Sansterminal contributors. Licensed under the MIT license.
# Run only against a dedicated development-package test process.
# WorkspacePath must be a new directory. Fixtures and diagnostic evidence are kept.
# UIA identifies the editor; every key is gated by the supplied window and native editor focus.
# The isolated test profile binds Ctrl+Alt+F9/F10/F11/F12 to files/terminal/editor/tabs,
# Ctrl+Alt+L to openWorkspaceLayout, and Ctrl+Alt+Backspace to closeTab.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateRange(1, [int]::MaxValue)][int]$ProbeProcessId,
    [Parameter(Mandatory)][string]$LogPath,
    [Parameter(Mandatory)][string]$WorkspacePath,
    [string]$ScreenshotPath,
    [string]$SettingsPath,
    [switch]$LibraryOnly,
    [switch]$Close
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
if (-not ('WorkspaceEditorNative' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class WorkspaceEditorNative {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr state);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct KeyInput { public ushort Key, Scan; public uint Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Sequential)] public struct MouseInput { public int X, Y; public uint Data, Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Explicit)] public struct InputData { [FieldOffset(0)] public KeyInput Key; [FieldOffset(0)] public MouseInput Mouse; }
    [StructLayout(LayoutKind.Sequential)] public struct Input { public uint Type; public InputData Data; }
    [StructLayout(LayoutKind.Sequential)] struct WindowMessage { public IntPtr Window; public uint Message; public UIntPtr WParam; public IntPtr LParam; public uint Time; public int X, Y; public uint Private; }
    [StructLayout(LayoutKind.Sequential)] struct GuiThreadInfo { public uint Size, Flags; public IntPtr Active, Focus, Capture, Menu, MoveSize, Caret; public Rect CaretRect; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr state);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hwnd, EnumProc callback, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder value, int size);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder value, int size);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsChild(IntPtr parent, IntPtr child);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, uint attribute, out Rect rect, uint size);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint source, uint target, bool attach);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] static extern bool PeekMessage(out WindowMessage message, IntPtr hwnd, uint min, uint max, uint remove);
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool GetGUIThreadInfo(uint thread, ref GuiThreadInfo info);
    public static IntPtr FocusedWindow() {
        var info = new GuiThreadInfo { Size=(uint)Marshal.SizeOf(typeof(GuiThreadInfo)) };
        return GetGUIThreadInfo(0, ref info) ? info.Focus : IntPtr.Zero;
    }
    [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint count, Input[] inputs, int size);
    static Input Key(ushort key, ushort scan, uint flags) { return new Input { Type=1, Data=new InputData { Key=new KeyInput { Key=key, Scan=scan, Flags=flags } } }; }
    public static void Chord(ushort[] keys) {
        var down = new Input[keys.Length];
        var up = new Input[keys.Length];
        for (int i=0; i<keys.Length; i++) { down[i]=Key(keys[i], 0, 0); up[keys.Length-1-i]=Key(keys[i], 0, 2); }
        var size = Marshal.SizeOf(typeof(Input));
        if (SendInput((uint)down.Length, down, size) != down.Length) throw new InvalidOperationException("SendInput was rejected");
        // WebView2 forwards accelerators to the host across an input queue.
        // Keep modifiers down while that callback samples GetKeyState, just
        // as with a physical key press, rather than releasing the whole chord
        // before either thread has processed its keydown.
        System.Threading.Thread.Sleep(80);
        if (SendInput((uint)up.Length, up, size) != up.Length) throw new InvalidOperationException("SendInput was rejected");
    }
    public static void Unicode(char character) {
        var inputs = new[] { Key(0, character, 4), Key(0, character, 6) };
        if (SendInput(2, inputs, Marshal.SizeOf(typeof(Input))) != 2) throw new InvalidOperationException("Unicode input was rejected");
    }
    public static void MouseButton(bool down) {
        var input = new Input { Type=0, Data=new InputData { Mouse=new MouseInput { Flags=down ? 2U : 4U } } };
        if (SendInput(1, new[] { input }, Marshal.SizeOf(typeof(Input))) != 1) throw new InvalidOperationException("Mouse input was rejected");
    }
    public static void MouseMove(int deltaX, int deltaY) {
        var input = new Input { Type=0, Data=new InputData { Mouse=new MouseInput { X=deltaX, Y=deltaY, Flags=1U } } };
        if (SendInput(1, new[] { input }, Marshal.SizeOf(typeof(Input))) != 1) throw new InvalidOperationException("Mouse motion was rejected");
    }
    public static void MouseMoveTo(int x, int y) {
        var absoluteX = (int)((long)(x - GetSystemMetrics(76)) * 65535 / Math.Max(1, GetSystemMetrics(78) - 1));
        var absoluteY = (int)((long)(y - GetSystemMetrics(77)) * 65535 / Math.Max(1, GetSystemMetrics(79) - 1));
        var input = new Input { Type=0, Data=new InputData { Mouse=new MouseInput { X=absoluteX, Y=absoluteY, Flags=0xC001U } } };
        if (SendInput(1, new[] { input }, Marshal.SizeOf(typeof(Input))) != 1) throw new InvalidOperationException("Absolute mouse motion was rejected");
    }
    public static bool ActivateWindow(IntPtr window) {
        ShowWindow(window, 9);
        SetWindowPos(window, new IntPtr(-1), 0, 0, 0, 0, 0x13);
        if (GetForegroundWindow() == window) return true;
        WindowMessage message;
        PeekMessage(out message, IntPtr.Zero, 0, 0, 0); // Create this worker's input queue.
        uint process;
        var foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), out process);
        var targetThread = GetWindowThreadProcessId(window, out process);
        var currentThread = GetCurrentThreadId();
        var attachedForeground = foregroundThread != currentThread && AttachThreadInput(currentThread, foregroundThread, true);
        var attachedTarget = targetThread != currentThread && targetThread != foregroundThread && AttachThreadInput(currentThread, targetThread, true);
        try { BringWindowToTop(window); SetForegroundWindow(window); return GetForegroundWindow() == window; }
        finally {
            if (attachedTarget) AttachThreadInput(currentThread, targetThread, false);
            if (attachedForeground) AttachThreadInput(currentThread, foregroundThread, false);
        }
    }
}
'@
}

$elementType = [System.Windows.Automation.AutomationElement]
$scope = [System.Windows.Automation.TreeScope]::Descendants
$results = [Collections.Generic.List[object]]::new()
$fixture = [IO.Path]::GetFullPath($WorkspacePath)
$logFile = [IO.Path]::GetFullPath($LogPath)
$script:testHandle = [IntPtr]::Zero
$script:testRoot = $null
$script:webRoot = $null
$script:surfaceHandle = [IntPtr]::Zero
$script:fixtureCreated = $false
$originalBounds = $null

function Assert-Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $results.Add([pscustomobject]@{ check = $Message; result = 'PASS' })
    Write-Output "PASS: $Message"
}
function Wait-Check([scriptblock]$Condition, [string]$Message, [int]$Seconds = 15) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        if (& $Condition) { Assert-Check $true $Message; return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timeout: $Message"
}
function Find-Control($Parent, [string]$Id = '', [string]$Name = '', $Type = $null, [switch]$IncludeOffscreen) {
    if (-not $Parent) { return $null }
    $conditions = [Collections.Generic.List[System.Windows.Automation.Condition]]::new()
    if ($Id) { $conditions.Add([System.Windows.Automation.PropertyCondition]::new($elementType::AutomationIdProperty, $Id)) }
    if ($Name) { $conditions.Add([System.Windows.Automation.PropertyCondition]::new($elementType::NameProperty, $Name)) }
    if ($Type) { $conditions.Add([System.Windows.Automation.PropertyCondition]::new($elementType::ControlTypeProperty, $Type)) }
    if (-not $IncludeOffscreen) { $conditions.Add([System.Windows.Automation.PropertyCondition]::new($elementType::IsOffscreenProperty, $false)) }
    $condition = if ($conditions.Count -eq 1) { $conditions[0] } else { [System.Windows.Automation.AndCondition]::new($conditions.ToArray()) }
    return $Parent.FindFirst($scope, $condition)
}
function Invoke-Control($Element) {
    if (-not $Element) { throw 'Required UIA control is missing' }
    $invoke = $null
    if ($Element.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$invoke)) {
        $invoke.Invoke()
        return
    }
    $toggle = $null
    if ($Element.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$toggle)) {
        $toggle.Toggle()
        return
    }
    throw "Control has no invoke or toggle pattern: $($Element.Current.AutomationId)"
}
function Get-WindowClass([IntPtr]$Handle) {
    $value = [Text.StringBuilder]::new(256)
    [void][WorkspaceEditorNative]::GetClassName($Handle, $value, 256)
    return $value.ToString()
}
function Connect-XamlRoot {
    # The top-level HWND's UIA provider may point at a XAML tooltip or popup.
    # Anchor queries to its actual content island, which remains stable.
    $script:testRoot = $elementType::FromHandle($script:testHandle)
    $children = [Collections.Generic.List[IntPtr]]::new()
    [void][WorkspaceEditorNative]::EnumChildWindows($script:testHandle, { param($child, $state) $children.Add($child); return $true }, [IntPtr]::Zero)
    foreach ($child in $children) {
        if ((Get-WindowClass $child) -eq 'Windows.UI.Composition.DesktopWindowContentBridge') {
            $script:testRoot = $elementType::FromHandle($child)
            return
        }
    }
}
function Test-OwnedWindow([IntPtr]$Handle) {
    if ($Handle -eq $script:testHandle -or [WorkspaceEditorNative]::IsChild($script:testHandle, $Handle)) { return $true }
    [uint32]$ownerProcess = 0
    [void][WorkspaceEditorNative]::GetWindowThreadProcessId($Handle, [ref]$ownerProcess)
    if ($ownerProcess -ne $ProbeProcessId) { return $false }
    for ($current = $Handle; $current -ne [IntPtr]::Zero; $current = [WorkspaceEditorNative]::GetWindow($current, 4)) {
        if ($current -eq $script:testHandle) { return $true }
    }
    return $false
}
function Assert-InputTarget([switch]$Editor) {
    if (-not (Test-OwnedWindow ([WorkspaceEditorNative]::GetForegroundWindow()))) {
        throw 'Input stopped: foreground changed away from the dedicated test window or its owned dialog'
    }
    if ($Editor) {
        $focusedWindow = [WorkspaceEditorNative]::FocusedWindow()
        if ($focusedWindow -ne $script:surfaceHandle -and -not [WorkspaceEditorNative]::IsChild($script:surfaceHandle, $focusedWindow)) {
            throw 'Input stopped: native keyboard focus left the dedicated editor surface'
        }
    }
}
function Send-Chord([UInt16[]]$Keys, [switch]$Editor) {
    Assert-InputTarget -Editor:$Editor
    [WorkspaceEditorNative]::Chord($Keys)
}
function Assert-PointerTarget {
    $point = [WorkspaceEditorNative+Point]::new()
    [void][WorkspaceEditorNative]::GetCursorPos([ref]$point)
    if (-not (Test-OwnedWindow ([WorkspaceEditorNative]::WindowFromPoint($point)))) {
        throw 'Mouse input stopped: the dedicated test window is obscured at the pointer'
    }
}
function Send-Text([string]$Text, [switch]$Editor) {
    foreach ($character in $Text.ToCharArray()) {
        Assert-InputTarget -Editor:$Editor
        [WorkspaceEditorNative]::Unicode($character)
    }
}
function Focus-Editor {
    if (-not [WorkspaceEditorNative]::ActivateWindow($script:testHandle)) { throw 'Cannot activate dedicated test window' }
    $editor = Find-Control $script:webRoot -Name '工作区文件编辑器' -Type ([System.Windows.Automation.ControlType]::Edit)
    if (-not $editor) { throw 'Monaco accessible textarea is missing' }
    $editor.SetFocus()
    Start-Sleep -Milliseconds 150
    Wait-Check {
        $focused = $elementType::FocusedElement.Current
        $focused.Name -eq '工作区文件编辑器' -and $focused.ControlType -eq [System.Windows.Automation.ControlType]::Edit -and $focused.HasKeyboardFocus
    } 'Monaco has keyboard focus'
    Assert-InputTarget -Editor
}
function Replace-EditorText([string]$Text) {
    Focus-Editor
    Send-Chord @(0x11, 0x41) -Editor
    Send-Text $Text -Editor
    Start-Sleep -Milliseconds 200
    Wait-Check {
        $tab = Get-DocumentTab $script:activeDocumentName
        if (-not $tab) { return $false }
        $titles = $tab.FindAll($scope, [System.Windows.Automation.PropertyCondition]::new($elementType::ControlTypeProperty, [System.Windows.Automation.ControlType]::Text))
        @($titles | Where-Object { $_.Current.Name.EndsWith(' *') }).Count -gt 0
    } 'Native document model acknowledges the edit before saving or closing'
}
function Save-Editor {
    Focus-Editor
    Send-Chord @(0x11, 0x53) -Editor
}
function Get-ProbeLog { return [IO.File]::ReadAllText($logFile) }
function Get-NativeRect([IntPtr]$Handle) {
    $rect = [WorkspaceEditorNative+Rect]::new()
    if (-not [WorkspaceEditorNative]::GetWindowRect($Handle, [ref]$rect)) { throw 'Cannot read native window bounds' }
    return $rect
}
function Get-CaptureRect([IntPtr]$Handle) {
    $rect = [WorkspaceEditorNative+Rect]::new()
    if ([WorkspaceEditorNative]::DwmGetWindowAttribute($Handle, 9, [ref]$rect, 16) -eq 0) { return $rect }
    return Get-NativeRect $Handle
}
function Connect-EditorSurface {
    $children = [Collections.Generic.List[IntPtr]]::new()
    [void][WorkspaceEditorNative]::EnumChildWindows($script:testHandle, { param($child, $state) $children.Add($child); return $true }, [IntPtr]::Zero)
    foreach ($child in $children) {
        $title = [Text.StringBuilder]::new(256)
        [void][WorkspaceEditorNative]::GetWindowText($child, $title, 256)
        if ($title.ToString() -eq 'Sansterminal EditorHostProbe') { $script:surfaceHandle = $child }
    }
    foreach ($child in $children) {
        if ((Get-WindowClass $child) -in @('Chrome_WidgetWin_0', 'Chrome_WidgetWin_1') -and [WorkspaceEditorNative]::IsChild($script:surfaceHandle, $child)) {
            $candidate = $elementType::FromHandle($child)
            if (Find-Control $candidate -Id 'RootWebArea' -IncludeOffscreen) {
                $script:webRoot = $candidate
                return $true
            }
        }
    }
    return $false
}
function Get-Terminal {
    return $script:testRoot.FindFirst($scope, [System.Windows.Automation.AndCondition]::new(
        [System.Windows.Automation.PropertyCondition]::new($elementType::ClassNameProperty, 'TermControl'),
        [System.Windows.Automation.PropertyCondition]::new($elementType::IsOffscreenProperty, $false)))
}
function Drag-Thumb([string]$Id, [int]$DeltaX, [int]$DeltaY) {
    if (-not [WorkspaceEditorNative]::ActivateWindow($script:testHandle)) { throw 'Cannot activate dedicated window for resizing' }
    Assert-InputTarget
    $thumb = Find-Control $script:testRoot -Id $Id
    if (-not $thumb) { throw "Resize handle is missing: $Id" }
    $bounds = $thumb.Current.BoundingRectangle
    $x = [int]($bounds.Left + $bounds.Width / 2)
    $y = [int]($bounds.Top + $bounds.Height / 2)
    $previous = [WorkspaceEditorNative+Point]::new()
    [void][WorkspaceEditorNative]::GetCursorPos([ref]$previous)
    [void][WorkspaceEditorNative]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 100
    Assert-PointerTarget
    [WorkspaceEditorNative]::MouseButton($true)
    try {
        $movedX = 0
        $movedY = 0
        for ($step = 1; $step -le 6; $step++) {
            Assert-InputTarget
            $nextX = [int]($DeltaX * $step / 6)
            $nextY = [int]($DeltaY * $step / 6)
            [WorkspaceEditorNative]::MouseMove(($nextX - $movedX), ($nextY - $movedY))
            $movedX = $nextX
            $movedY = $nextY
            Start-Sleep -Milliseconds 30
        }
    } finally {
        [WorkspaceEditorNative]::MouseButton($false)
        if (Test-OwnedWindow ([WorkspaceEditorNative]::GetForegroundWindow())) { [void][WorkspaceEditorNative]::SetCursorPos($previous.X, $previous.Y) }
    }
    Start-Sleep -Milliseconds 200
}
function Set-LayoutChoice([string]$Id, [int]$Index) {
    $combo = Find-Control $script:testRoot -Id $Id -IncludeOffscreen
    if (-not $combo) { throw "Layout choice is missing: $Id" }
    if (-not [WorkspaceEditorNative]::ActivateWindow($script:testHandle)) { throw 'Cannot activate dedicated settings window' }
    $combo.SetFocus()
    Send-Chord @(0x12, 0x28)
    Send-Chord @(0x24)
    for ($choice = 0; $choice -lt $Index; $choice++) { Send-Chord @(0x28) }
    Send-Chord @(0x0D)
}
function Open-LayoutSettings {
    if (-not [WorkspaceEditorNative]::ActivateWindow($script:testHandle)) { throw 'Cannot activate dedicated window for settings' }
    Send-Chord @(0x1B)
    Wait-Check { $null -ne (Find-Control $script:testRoot -Id 'WorkspaceLayoutButton') } 'Layout settings entry is available'
    Invoke-Control (Find-Control $script:testRoot -Id 'WorkspaceLayoutButton')
    Wait-Check { $null -ne (Find-Control $script:testRoot -Id 'WorkspaceLayoutSource' -IncludeOffscreen) } 'Layout button opens the dedicated workspace settings page'
    Assert-Check (-not [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle)) 'Native editor yields its surface to the settings page'
}
function Close-LayoutSettings {
    if (-not (Find-Control $script:testRoot -Id 'WorkspaceLayoutSource' -IncludeOffscreen)) { throw 'Input stopped: the selected content is not workspace settings' }
    $button = Find-Control $script:testRoot -Id 'SaveButton'
    if ($button) { $button.SetFocus() }
    Start-Sleep -Milliseconds 150
    Send-Chord @(0x11, 0x12, 0x08)
    Wait-Check {
        $null -ne (Find-Control $script:testRoot -Id 'WorkspaceLayoutButton') -and
        $null -eq (Find-Control $script:testRoot -Id 'WorkspaceLayoutSource' -IncludeOffscreen)
    } 'Closing settings returns to the same workspace'
}
function Compose-Layout([int]$Source, [int]$Target, [int]$Side) {
    Set-LayoutChoice 'WorkspaceLayoutSource' $Source
    Set-LayoutChoice 'WorkspaceLayoutTarget' $Target
    Set-LayoutChoice 'WorkspaceLayoutSide' $Side
    Invoke-Control (Find-Control $script:testRoot -Id 'WorkspaceLayoutMove' -IncludeOffscreen)
    $scroll = Find-Control $script:testRoot -Id 'SettingsMainPage_ScrollViewer' -IncludeOffscreen
    $pattern = $null
    if ($scroll -and $scroll.TryGetCurrentPattern([System.Windows.Automation.ScrollPattern]::Pattern, [ref]$pattern)) {
        $pattern.SetScrollPercent(-1, 0)
    }
}
function Save-LayoutSettings {
    Invoke-Control (Find-Control $script:testRoot -Id 'SaveButton')
    Start-Sleep -Milliseconds 500
    # XAML tooltips temporarily replace the UIA tree exposed by an island.
    # Dismiss the tooltip before querying the settings tree after hot reload.
    $bounds = Get-NativeRect $script:testHandle
    [WorkspaceEditorNative]::MouseMoveTo(($bounds.Right - 180), ($bounds.Top + 20))
    Send-Chord @(0x1B)
    Wait-Check { $null -ne (Find-Control $script:testRoot -Id 'SaveButton') } 'Settings controls are ready after saving and hot reload'
}
function Drag-PreviewAbove([string]$Source, [string]$Target) {
    $from = (Find-Control $script:testRoot -Id $Source).Current.BoundingRectangle
    $to = (Find-Control $script:testRoot -Id $Target).Current.BoundingRectangle
    if ($from.IsEmpty -or $to.IsEmpty) { throw 'Preview drag source or target is not visible' }
    if (-not [WorkspaceEditorNative]::ActivateWindow($script:testHandle)) { throw 'Cannot activate settings preview for dragging' }
    $startX = [int]($from.Left + $from.Width / 2)
    $startY = [int]($from.Top + $from.Height / 2)
    $endX = [int]($to.Left + $to.Width / 2)
    $endY = [int]($to.Top + 12)
    Assert-InputTarget
    [WorkspaceEditorNative]::MouseMoveTo($startX, $startY)
    Start-Sleep -Milliseconds 100
    Assert-PointerTarget
    [WorkspaceEditorNative]::MouseButton($true)
    try {
        for ($step = 1; $step -le 12; $step++) {
            Assert-InputTarget
            [WorkspaceEditorNative]::MouseMoveTo([int]($startX + ($endX - $startX) * $step / 12), [int]($startY + ($endY - $startY) * $step / 12))
            Start-Sleep -Milliseconds 45
        }
    } finally { [WorkspaceEditorNative]::MouseButton($false) }
}
function Test-WorkspaceLayouts {
    Open-LayoutSettings
    Drag-PreviewAbove 'WorkspacePreviewEditor' 'WorkspacePreviewTerminal'
    Wait-Check {
        $editor = Find-Control $script:testRoot -Id 'WorkspacePreviewEditor'
        $terminal = Find-Control $script:testRoot -Id 'WorkspacePreviewTerminal'
        return $editor -and $terminal -and $editor.Current.BoundingRectangle.Top -lt $terminal.Current.BoundingRectangle.Top
    } 'Dragging preview editor above terminal composes a nested layout'
    Compose-Layout 2 1 2
    Wait-Check {
        $editor = Find-Control $script:testRoot -Id 'WorkspacePreviewEditor' -IncludeOffscreen
        $terminal = Find-Control $script:testRoot -Id 'WorkspacePreviewTerminal' -IncludeOffscreen
        if (-not $editor -or -not $terminal) { return $false }
        return $editor.Current.BoundingRectangle.Top -lt $terminal.Current.BoundingRectangle.Top
    } 'Settings preview composes editor above terminal'
    $rootRatio = Find-Control $script:testRoot -Id 'WorkspaceLayoutRootRatio' -IncludeOffscreen
    $innerRatio = Find-Control $script:testRoot -Id 'WorkspaceLayoutInnerRatio' -IncludeOffscreen
    $rootRatio.GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern).SetValue(30)
    $innerRatio.GetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern).SetValue(58)
    Save-LayoutSettings
    if ($SettingsPath) {
        $saved = [IO.File]::ReadAllText([IO.Path]::GetFullPath($SettingsPath)) | ConvertFrom-Json
        Assert-Check ($saved.workspaceLayout -is [pscustomobject]) 'Saved workspace layout is a JSON tree object'
        Assert-Check (($saved.workspaceLayout | ConvertTo-Json -Depth 10 -Compress) -match 'column') 'Nested vertical split persists in settings'
        Assert-Check ([Math]::Abs($saved.workspaceLayout.ratio - 0.3) -lt 0.001 -and [Math]::Abs($saved.workspaceLayout.second.ratio - 0.58) -lt 0.001) 'Both divider proportions persist through the settings page'
    }
    Close-LayoutSettings
    Wait-Check {
        $files = Find-Control $script:testRoot -Id 'WorkspaceFileSearchBox'
        $terminal = Get-Terminal
        if (-not $files -or -not $terminal -or -not [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle)) { return $false }
        $editor = Get-NativeRect $script:surfaceHandle
        return $files.Current.BoundingRectangle.Left -lt $editor.Left -and $editor.Top -lt $terminal.Current.BoundingRectangle.Top
    } 'Workspace applies nested layout: file tree left, editor above terminal'
    $editorBefore = Get-NativeRect $script:surfaceHandle
    Drag-Thumb 'WorkspaceDocumentDivider' 0 35
    Wait-Check {
        $after = Get-NativeRect $script:surfaceHandle
        [Math]::Abs(($after.Bottom - $after.Top) - ($editorBefore.Bottom - $editorBefore.Top)) -gt 10
    } 'Nested horizontal divider resizes the native editor with real mouse input'
    $filesBefore = (Find-Control $script:testRoot -Id 'WorkspaceFileSearchBox').Current.BoundingRectangle
    Drag-Thumb 'WorkspaceFilesDivider' 36 0
    Wait-Check { (Find-Control $script:testRoot -Id 'WorkspaceFileSearchBox').Current.BoundingRectangle.Width -gt $filesBefore.Width + 10 } 'Outer divider resizes the file tree beside the nested group'
    $sideBefore = (Find-Control $script:testRoot -Id 'SideTabDivider').Current.BoundingRectangle.Left
    Drag-Thumb 'SideTabDivider' 36 0
    Wait-Check { (Find-Control $script:testRoot -Id 'SideTabDivider').Current.BoundingRectangle.Left -gt $sideBefore + 10 } 'Fixed left tabs respond to mouse width resizing'

    Open-LayoutSettings
    $savedLayoutText = if ($SettingsPath) { [IO.File]::ReadAllText([IO.Path]::GetFullPath($SettingsPath)) } else { '' }
    Compose-Layout 0 2 1
    Invoke-Control (Find-Control $script:testRoot -Id 'ResetButton')
    Wait-Check {
        $files = Find-Control $script:testRoot -Id 'WorkspacePreviewFiles' -IncludeOffscreen
        $editor = Find-Control $script:testRoot -Id 'WorkspacePreviewEditor' -IncludeOffscreen
        if (-not $files -or -not $editor) { return $false }
        return $files.Current.BoundingRectangle.Left -lt $editor.Current.BoundingRectangle.Left
    } 'Reset discards the unsaved layout draft and reloads the saved arrangement'
    if ($SettingsPath) {
        Assert-Check ([IO.File]::ReadAllText([IO.Path]::GetFullPath($SettingsPath)) -eq $savedLayoutText) 'Composing and resetting a draft leaves saved settings unchanged'
    }
    Compose-Layout 0 2 1
    Save-LayoutSettings
    Close-LayoutSettings
    Wait-Check {
        $files = Find-Control $script:testRoot -Id 'WorkspaceFileSearchBox'
        $terminal = Get-Terminal
        if (-not $files -or -not $terminal -or -not [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle)) { return $false }
        $editor = Get-NativeRect $script:surfaceHandle
        return $files.Current.BoundingRectangle.Left -gt $editor.Left -and $terminal.Current.BoundingRectangle.Top -gt $editor.Top
    } 'Moving the file tree right of editor composes a different nested layout'

    # The temporary portable settings bind Ctrl+Alt+F9..F12 to the four
    # visibility actions; no default application shortcuts are changed.
    foreach ($pane in @(
        @{ name='files'; key=0x78; visible={ $null -ne (Find-Control $script:testRoot -Id 'WorkspaceFileSearchBox') } },
        @{ name='terminal'; key=0x79; visible={ $null -ne (Get-Terminal) } },
        @{ name='tabs'; key=0x7B; visible={ $null -ne (Find-Control $script:testRoot -Id 'SideTabDivider') } }
    )) {
        Focus-Editor
        Send-Chord @(0x11, 0x12, $pane.key) -Editor
        Wait-Check { -not (& $pane.visible) } "Configured shortcut hides $($pane.name) while Monaco has focus"
        Focus-Editor
        Send-Chord @(0x11, 0x12, $pane.key) -Editor
        Wait-Check $pane.visible "Configured shortcut restores $($pane.name) without reopening its session"
    }
    Focus-Editor
    Send-Chord @(0x11, 0x12, 0x7A) -Editor
    Wait-Check { -not [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } 'Configured shortcut hides the focused editor'
    $header = Find-Control $script:testRoot -Id 'WorkspaceLayoutButton'
    $header.SetFocus()
    Send-Chord @(0x11, 0x12, 0x7A)
    Wait-Check { [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } 'Configured shortcut restores editor from the header'
    Focus-Editor
    Send-Chord @(0x11, 0x12, 0x4C) -Editor
    Wait-Check { $null -ne (Find-Control $script:testRoot -Id 'WorkspaceLayoutSource' -IncludeOffscreen) } 'Configured shortcut opens workspace layout settings from Monaco'
    foreach ($id in @('WorkspaceShowFiles', 'WorkspaceShowTerminal', 'WorkspaceShowEditor', 'WorkspaceShowTabs')) {
        Invoke-Control (Find-Control $script:testRoot -Id $id -IncludeOffscreen)
    }
    Save-LayoutSettings
    Assert-Check ($null -ne (Find-Control $script:testRoot -Id 'SaveButton')) 'Settings remains reachable when all workspace regions are hidden'
    Close-LayoutSettings
    Wait-Check {
        $null -eq (Find-Control $script:testRoot -Id 'WorkspaceFileSearchBox') -and $null -eq (Get-Terminal) -and
        $null -eq (Find-Control $script:testRoot -Id 'SideTabDivider') -and -not [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle)
    } 'Settings independently hides all four workspace regions'
    Open-LayoutSettings
    foreach ($id in @('WorkspaceShowFiles', 'WorkspaceShowTerminal', 'WorkspaceShowEditor', 'WorkspaceShowTabs')) {
        Invoke-Control (Find-Control $script:testRoot -Id $id -IncludeOffscreen)
    }
    Invoke-Control (Find-Control $script:testRoot -Id 'WorkspaceLayoutReset' -IncludeOffscreen)
    Save-LayoutSettings
    Close-LayoutSettings
    Wait-Check { $null -ne (Get-Terminal) -and $null -ne (Find-Control $script:testRoot -Id 'WorkspaceFileSearchBox') -and [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } 'Re-enabling regions restores the existing terminal, tree and editor'
}
function Find-OwnedPicker {
    $found = [Collections.Generic.List[IntPtr]]::new()
    [void][WorkspaceEditorNative]::EnumWindows({ param($candidate, $state)
        if ((Test-OwnedWindow $candidate) -and (Get-WindowClass $candidate) -eq '#32770' -and [WorkspaceEditorNative]::IsWindowVisible($candidate)) {
            $found.Add($candidate)
        }
        return $true
    }, [IntPtr]::Zero)
    if ($found.Count -eq 1) { return $found[0] }
    return [IntPtr]::Zero
}
function Show-WorkspaceHub {
    $button = Find-Control $script:testRoot -Id 'WorkspaceHomeButton'
    if (-not $button) { $button = Find-Control $script:testRoot -Id 'WorkspaceHeaderHome' }
    if (-not $button) { $button = Find-Control $script:testRoot -Name '工作区' -Type ([System.Windows.Automation.ControlType]::Button) }
    if (-not $button) { $button = Find-Control $script:testRoot -Name 'Workspaces' -Type ([System.Windows.Automation.ControlType]::Button) }
    Invoke-Control $button
    Wait-Check { $null -ne (Find-Control $script:testRoot -Id 'WorkspaceHubFolderButton') } 'Workspace hub opens'
    Wait-Check {
        $page = Find-Control $script:testRoot -Id 'WorkspaceHomeViewport'
        if (-not $page) {
            $title = Find-Control $script:testRoot -Id 'WorkspaceHubTitle'
            if ($title) { $page = [System.Windows.Automation.TreeWalker]::RawViewWalker.GetParent($title) }
        }
        if (-not $page) { return $false }
        $outer = Get-NativeRect $script:testHandle
        $bounds = $page.Current.BoundingRectangle
        return [Math]::Abs($bounds.Left - $outer.Left) -lt 20 -and [Math]::Abs($outer.Right - $bounds.Right) -lt 20
    } 'Home page fills the window content width with no sidebar or transparent gap'
    $titleBounds = (Find-Control $script:testRoot -Id 'WorkspaceHubTitle').Current.BoundingRectangle
    $windowBounds = Get-NativeRect $script:testHandle
    Assert-Check ([Math]::Abs(($titleBounds.Left + $titleBounds.Width / 2) - ($windowBounds.Left + $windowBounds.Right) / 2) -lt 12) 'Home content is horizontally centered in the full window'
    Assert-Check ($null -eq (Find-Control $script:testRoot -Id 'SideTabDivider')) 'Home page hides workspace navigation and its resize handle'
}
function Open-WorkspaceFolder([string]$Path) {
    if (-not (Find-Control $script:testRoot -Id 'WorkspaceHubFolderButton')) { Show-WorkspaceHub }
    $folderButton = Find-Control $script:testRoot -Id 'WorkspaceHubFolderButton'
    if (-not [WorkspaceEditorNative]::ActivateWindow($script:testHandle)) { throw 'Cannot activate dedicated test window for picker' }
    $folderButton.SetFocus()
    Send-Chord @(0x0D)
    Wait-Check { (Find-OwnedPicker) -ne [IntPtr]::Zero } 'Folder picker belongs to dedicated test window'
    $pickerHandle = Find-OwnedPicker
    if (-not [WorkspaceEditorNative]::ActivateWindow($pickerHandle)) { throw 'Cannot activate owned folder picker' }
    Send-Chord @(0x12, 0x44)
    Wait-Check {
        $focusedWindow = [WorkspaceEditorNative]::FocusedWindow()
        [WorkspaceEditorNative]::IsChild($pickerHandle, $focusedWindow) -and (Get-WindowClass $focusedWindow) -eq 'Edit'
    } 'Picker address edit is ready for a path'
    Send-Text $Path
    Send-Chord @(0x0D)
    Start-Sleep -Milliseconds 350
    Assert-InputTarget
    [void][WorkspaceEditorNative]::PostMessage($pickerHandle, 0x111, [IntPtr]1, [IntPtr]::Zero)
    Wait-Check { -not [WorkspaceEditorNative]::IsWindow($pickerHandle) } 'Folder picker completes'
    Wait-Check { $null -ne (Find-Control $script:testRoot -Id 'WorkspaceFileSearchBox') -and $null -ne (Get-FileTree) } 'Workspace file tree is visible'
}
function Get-FileTree {
    $tree = Find-Control $script:testRoot -Id 'WorkspaceFileTree'
    if ($tree) { return $tree }
    # WinUI 2 TreeView exposes its inner ListControl peer, not the XAML name.
    # Files are the final visible tree following the workspace navigation tree.
    $trees = $script:testRoot.FindAll($scope, [System.Windows.Automation.AndCondition]::new(
        [System.Windows.Automation.PropertyCondition]::new($elementType::ControlTypeProperty, [System.Windows.Automation.ControlType]::Tree),
        [System.Windows.Automation.PropertyCondition]::new($elementType::IsOffscreenProperty, $false)))
    if ($trees.Count) { return $trees[$trees.Count - 1] }
    return $null
}
function Open-TreeFile([string]$Name) {
    if (-not [WorkspaceEditorNative]::ActivateWindow($script:testHandle)) { throw 'Cannot activate dedicated workspace for opening a file' }
    $tree = Get-FileTree
    Wait-Check { $null -ne (Find-Control $tree -Name $Name) } "File tree contains $Name"
    $item = Find-Control $tree -Name $Name -Type ([System.Windows.Automation.ControlType]::TreeItem)
    if (-not $item) {
        $item = Find-Control $tree -Name $Name
        while ($item -and $item.Current.ControlType -ne [System.Windows.Automation.ControlType]::TreeItem) {
            $item = [System.Windows.Automation.TreeWalker]::RawViewWalker.GetParent($item)
        }
    }
    if (-not $item) { throw "Cannot identify TreeViewItem for $Name" }
    $invoke = $null
    if ($item.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$invoke)) {
        $invoke.Invoke()
    } else {
        [void][WorkspaceEditorNative]::SetForegroundWindow($script:testHandle)
        $item.SetFocus()
        Send-Chord @(0x0D)
    }
    if (-not $script:webRoot) {
        Wait-Check { (Get-ProbeLog) -match 'ready: workspace document bridge' } 'Native document bridge is ready after opening the first file' 25
        Wait-Check { Connect-EditorSurface } 'Native WebView2 surface exists in the dedicated window'
    }
    Wait-Check { [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } "Native editor displays $Name"
    Wait-Check {
        $names = $script:webRoot.FindAll($scope, [System.Windows.Automation.Condition]::TrueCondition)
        @($names | Where-Object { $_.Current.Name -like "*$Name*" }).Count -gt 0
    } "Bridge activates $Name"
    $script:activeDocumentName = $Name
}
function Get-DocumentTab([string]$Name) {
    $tabs = Find-Control $script:testRoot -Id 'WorkspaceDocumentTabs'
    if (-not $tabs) { return $null }
    $items = $tabs.FindAll($scope, [System.Windows.Automation.PropertyCondition]::new($elementType::ControlTypeProperty, [System.Windows.Automation.ControlType]::TabItem))
    foreach ($item in $items) {
        if ($item.Current.Name -like "*$Name*" -or (Find-Control $item -Name $Name)) { return $item }
    }
    return $null
}
function Request-DocumentClose([string]$Name) {
    $tab = Get-DocumentTab $Name
    if (-not $tab) { throw "Document tab is missing: $Name" }
    $button = Find-Control $tab -Id 'CloseButton'
    if (-not $button) { $button = Find-Control $tab -Id 'CloseTabButton' }
    if (-not $button) { $button = Find-Control $tab -Type ([System.Windows.Automation.ControlType]::Button) }
    Invoke-Control $button
}
function Get-UnsavedDialog { return Find-Control $script:testRoot -Id 'WorkspaceEditorUnsavedDialog' }
function Invoke-UnsavedChoice([string]$Name) {
    $dialog = Get-UnsavedDialog
    if (-not $dialog) { throw 'Unsaved changes dialog is missing' }
    Invoke-Control (Find-Control $dialog -Name $Name -Type ([System.Windows.Automation.ControlType]::Button))
}
function Write-Diagnostics {
    if (-not $script:fixtureCreated) { return }
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($parent in @($script:testRoot, $script:webRoot)) {
        if (-not $parent) { continue }
        try {
            foreach ($element in $parent.FindAll($scope, [System.Windows.Automation.Condition]::TrueCondition)) {
                $current = $element.Current
                $lines.Add("$($current.ControlType.ProgrammaticName) | id=$($current.AutomationId) | name=$($current.Name) | process=$($current.ProcessId) | offscreen=$($current.IsOffscreen)")
            }
        } catch { $lines.Add("UIA diagnostic error: $_") }
    }
    [IO.File]::WriteAllLines((Join-Path $fixture 'ui-automation.txt'), $lines)
    if (Test-Path -LiteralPath $logFile) { Copy-Item -LiteralPath $logFile -Destination (Join-Path $fixture 'probe.log') }
}

if ($LibraryOnly) { return }

try {
    Assert-Check (Test-Path -LiteralPath $logFile -PathType Leaf) 'Explicit probe log exists'
    Assert-Check ((Get-ProbeLog) -match 'created: one workspace editor host; native document bridge') 'Log identifies a workspace editor host'
    $windows = [Collections.Generic.List[IntPtr]]::new()
    [void][WorkspaceEditorNative]::EnumWindows({ param($candidate, $state)
        [uint32]$ownerProcess = 0
        [void][WorkspaceEditorNative]::GetWindowThreadProcessId($candidate, [ref]$ownerProcess)
        if ($ownerProcess -eq $ProbeProcessId -and (Get-WindowClass $candidate) -eq 'CASCADIA_HOSTING_WINDOW_CLASS') { $windows.Add($candidate) }
        return $true
    }, [IntPtr]::Zero)
    Assert-Check ($windows.Count -eq 1) 'Supplied process owns exactly one dedicated test window'
    $script:testHandle = $windows[0]
    Connect-XamlRoot
    Assert-Check (-not (Test-Path -LiteralPath $fixture)) 'Explicit fixture directory is new; no existing files will be overwritten'
    $workspaceA = Join-Path $fixture 'workspace-a'
    $workspaceB = Join-Path $fixture 'workspace-b'
    [void][IO.Directory]::CreateDirectory($workspaceA)
    $script:fixtureCreated = $true
    [void][IO.Directory]::CreateDirectory($workspaceB)
    $alphaFile = Join-Path $workspaceA 'alpha.txt'
    $conflictFile = Join-Path $workspaceA 'conflict.txt'
    [IO.File]::WriteAllText($alphaFile, "original-alpha`r`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $workspaceA 'beta.txt'), "original-beta`r`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($conflictFile, "original-conflict`r`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $workspaceB 'other.txt'), "other-workspace`r`n", [Text.UTF8Encoding]::new($false))
    $originalBounds = Get-NativeRect $script:testHandle
    [void][WorkspaceEditorNative]::SetWindowPos($script:testHandle, [IntPtr]::Zero, $originalBounds.Left, $originalBounds.Top, 1300, 760, 0x14)

    Open-WorkspaceFolder $workspaceA
    Open-TreeFile 'alpha.txt'
    $browserMatch = [regex]::Match((Get-ProbeLog), 'browser-process: (\d+)')
    Assert-Check $browserMatch.Success 'Owned browser process is recorded'
    $browserProcessId = [int]$browserMatch.Groups[1].Value
    $browser = Get-CimInstance Win32_Process -Filter "ProcessId = $browserProcessId"
    Assert-Check ($browser.ParentProcessId -eq $ProbeProcessId) 'Recorded browser process was created by the dedicated target process'
    $tab = Get-DocumentTab 'alpha.txt'
    Assert-Check ($null -ne $tab) 'File-tree invocation creates the existing native document tab'
    $inner = Get-NativeRect $script:surfaceHandle
    $outer = Get-NativeRect $script:testHandle
    $tabBounds = $tab.Current.BoundingRectangle
    Assert-Check ($inner.Left -ge $outer.Left -and $inner.Right -le $outer.Right -and $inner.Bottom -le $outer.Bottom -and $inner.Top -ge $tabBounds.Bottom - 2) 'WebView2 occupies document content below native document tabs'
    Replace-EditorText 'saved-alpha'
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'saved-alpha' } 'Ctrl+S writes the selected real file'

    Replace-EditorText 'dirty-alpha'
    Test-WorkspaceLayouts
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'dirty-alpha' } 'Edited text survives nested layout changes, settings navigation and visibility shortcuts'
    Replace-EditorText 'dirty-alpha-after-layout'
    Open-TreeFile 'beta.txt'
    Assert-Check ($null -ne (Get-DocumentTab 'alpha.txt')) 'Opening another file retains the edited document tab'
    Open-TreeFile 'alpha.txt'
    Show-WorkspaceHub
    Wait-Check { -not [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } 'Workspace hub suppresses the native editor surface'
    Open-WorkspaceFolder $workspaceB
    Open-TreeFile 'other.txt'
    Open-WorkspaceFolder $workspaceA
    Open-TreeFile 'alpha.txt'
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'dirty-alpha-after-layout' } 'Dirty text survives file and workspace switches'
    Focus-Editor
    Send-Chord @(0x11, 0x5A) -Editor
    Start-Sleep -Milliseconds 200
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'dirty-alpha' } 'Original edit history survives workspace switching'
    Focus-Editor
    Send-Chord @(0x11, 0x59) -Editor
    Start-Sleep -Milliseconds 200
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'dirty-alpha-after-layout' } 'Redo restores the pre-switch edit'
    Open-TreeFile 'conflict.txt'
    Replace-EditorText 'dirty-conflict'
    [IO.File]::WriteAllText($conflictFile, 'external-version', [Text.UTF8Encoding]::new($false))
    Save-Editor
    Wait-Check {
        $elements = $script:webRoot.FindAll($scope, [System.Windows.Automation.Condition]::TrueCondition)
        @($elements | Where-Object { $_.Current.Name -match '磁盘|冲突|changed|conflict' -and $_.Current.Name -notlike '*conflict.txt*' }).Count -gt 0
    } 'Save conflict is reported to the editor'
    Assert-Check ([IO.File]::ReadAllText($conflictFile) -eq 'external-version') 'Save conflict does not overwrite external file changes'
    Request-DocumentClose 'conflict.txt'
    Wait-Check { $null -ne (Get-UnsavedDialog) } 'Dirty document close opens native unsaved-changes dialog'
    Assert-Check (-not [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle)) 'Native editor yields to unsaved-changes dialog'
    Invoke-UnsavedChoice '取消'
    Wait-Check { [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } 'Cancel restores the same native editor'
    Assert-Check ($null -ne (Get-DocumentTab 'conflict.txt')) 'Cancel keeps the dirty document open'
    Request-DocumentClose 'conflict.txt'
    Wait-Check { $null -ne (Get-UnsavedDialog) } 'Save-on-close still detects unsaved changes'
    Invoke-UnsavedChoice '保存'
    Wait-Check { $null -eq (Get-UnsavedDialog) -and [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } 'Failed save-on-close returns to the editor'
    Assert-Check ($null -ne (Get-DocumentTab 'conflict.txt')) 'Save conflict cancels document close'
    Assert-Check ([IO.File]::ReadAllText($conflictFile) -eq 'external-version') 'Failed save-on-close preserves external disk changes'
    Request-DocumentClose 'conflict.txt'
    Wait-Check { $null -ne (Get-UnsavedDialog) } 'Second close still detects unsaved changes'
    Invoke-UnsavedChoice '放弃'
    Wait-Check { $null -eq (Get-DocumentTab 'conflict.txt') } 'Discard closes the selected dirty document'
    Assert-Check ([IO.File]::ReadAllText($conflictFile) -eq 'external-version') 'Discard preserves the external disk contents'

    Open-TreeFile 'beta.txt'
    Replace-EditorText 'save-on-close-beta'
    Request-DocumentClose 'beta.txt'
    Wait-Check { $null -ne (Get-UnsavedDialog) } 'Closing another edited document prompts to save'
    Invoke-UnsavedChoice '保存'
    Wait-Check { $null -eq (Get-DocumentTab 'beta.txt') } 'Successful save-on-close removes the document tab'
    Assert-Check ([IO.File]::ReadAllText((Join-Path $workspaceA 'beta.txt')) -eq 'save-on-close-beta') 'Save-on-close writes the complete buffer'
    Open-TreeFile 'alpha.txt'
    Replace-EditorText 'window-close-cancel-alpha'
    [void][WorkspaceEditorNative]::PostMessage($script:testHandle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    Wait-Check {
        # WinUI's built-in shell warning does not always expose its XAML name
        # as an automation ID. Its visible primary button is stable instead.
        if (-not (Get-UnsavedDialog)) {
            $primary = Find-Control $script:testRoot -Id 'PrimaryButton'
            if ($primary) { Invoke-Control $primary }
        }
        $null -ne (Get-UnsavedDialog)
    } 'Window close prompts for the current unsaved buffer'
    Invoke-UnsavedChoice '取消'
    Wait-Check { [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } 'Cancelling window close restores editing'
    Assert-Check ([WorkspaceEditorNative]::IsWindow($script:testHandle) -and $null -ne (Get-DocumentTab 'alpha.txt')) 'Cancelling window close retains the window and document'
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'window-close-cancel-alpha' } 'Cancelled window close retains all unsaved text'

    $log = Get-ProbeLog
    Assert-Check (([regex]::Matches($log, 'controller-created:')).Count -eq 1) 'One WebView2 controller serves all files, workspaces and dialogs'
    Assert-Check ($log -notmatch 'smoke-failed:|host-error:|page-error:') 'No host, page or self-test errors were reported'
    if ($ScreenshotPath) {
        Add-Type -AssemblyName System.Drawing
        if (-not [WorkspaceEditorNative]::ActivateWindow($script:testHandle)) { throw 'Cannot activate dedicated window for screenshot' }
        Assert-InputTarget
        $capture = Get-NativeRect $script:testHandle
        $bitmap = [Drawing.Bitmap]::new($capture.Right - $capture.Left, $capture.Bottom - $capture.Top)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $dc = $graphics.GetHdc()
            try {
                if (-not [WorkspaceEditorNative]::PrintWindow($script:testHandle, $dc, 2)) { throw 'Dedicated window capture failed' }
            } finally { $graphics.ReleaseHdc($dc) }
            $bitmap.Save([IO.Path]::GetFullPath($ScreenshotPath), [Drawing.Imaging.ImageFormat]::Png)
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
        Assert-Check (Test-Path -LiteralPath $ScreenshotPath -PathType Leaf) 'Dedicated window screenshot is saved before close'
    }
    if ($Close) {
        [void][WorkspaceEditorNative]::PostMessage($script:testHandle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
        # Multiple fixture workspaces may have created multiple terminals.
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while ([WorkspaceEditorNative]::IsWindow($script:testHandle) -and [DateTime]::UtcNow -lt $deadline) {
            if (Get-UnsavedDialog) { throw 'Unexpected dirty document remains at final close' }
            $primary = Find-Control $script:testRoot -Id 'PrimaryButton'
            if ($primary) { Invoke-Control $primary }
            Start-Sleep -Milliseconds 100
        }
        Wait-Check { (Get-ProbeLog) -match 'closed: controller released' } 'Closing the dedicated window releases its controller'
        Wait-Check { -not (Get-Process -Id $browserProcessId -ErrorAction SilentlyContinue) } 'Owned browser process exits after window close'
    } else {
        [void][WorkspaceEditorNative]::SetWindowPos($script:testHandle, [IntPtr](-2), $originalBounds.Left, $originalBounds.Top, $originalBounds.Right - $originalBounds.Left, $originalBounds.Bottom - $originalBounds.Top, 0x10)
        Write-Output 'Window remains open. Use -Close to verify controller teardown and browser exit.'
    }
    Write-Diagnostics
    $results | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $fixture 'results.json') -Encoding utf8
    Write-Output "Evidence retained at $fixture"
} catch {
    $results.Add([pscustomobject]@{ check = $_.Exception.Message; result = 'FAIL' })
    Write-Diagnostics
    if ($script:fixtureCreated) {
        $results | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $fixture 'results.json') -Encoding utf8
    }
    if ($originalBounds -and [WorkspaceEditorNative]::IsWindow($script:testHandle)) {
        [void][WorkspaceEditorNative]::SetWindowPos($script:testHandle, [IntPtr](-2), $originalBounds.Left, $originalBounds.Top, $originalBounds.Right - $originalBounds.Left, $originalBounds.Bottom - $originalBounds.Top, 0x10)
    }
    throw
}
