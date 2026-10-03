# Copyright (c) Sansterminal contributors. Licensed under the MIT license.
# Run only against a dedicated SANSTERMINAL_WORKSPACE_EDITOR=1 test process.
# WorkspacePath must be a new directory. Fixtures and diagnostic evidence are kept.
# UIA identifies the editor; every key is gated by the supplied window and native editor focus.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateRange(1, [int]::MaxValue)][int]$ProbeProcessId,
    [Parameter(Mandatory)][string]$LogPath,
    [Parameter(Mandatory)][string]$WorkspacePath,
    [string]$ScreenshotPath,
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
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
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
        var inputs = new Input[keys.Length * 2];
        for (int i=0; i<keys.Length; i++) { inputs[i]=Key(keys[i], 0, 0); inputs[inputs.Length-1-i]=Key(keys[i], 0, 2); }
        if (SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(Input))) != inputs.Length) throw new InvalidOperationException("SendInput was rejected");
    }
    public static void Unicode(char character) {
        var inputs = new[] { Key(0, character, 4), Key(0, character, 6) };
        if (SendInput(2, inputs, Marshal.SizeOf(typeof(Input))) != 2) throw new InvalidOperationException("Unicode input was rejected");
    }
    public static bool ActivateWindow(IntPtr window) {
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
function Find-Control($Parent, [string]$Id = '', [string]$Name = '', $Type = $null) {
    if (-not $Parent) { return $null }
    $conditions = [Collections.Generic.List[System.Windows.Automation.Condition]]::new()
    if ($Id) { $conditions.Add([System.Windows.Automation.PropertyCondition]::new($elementType::AutomationIdProperty, $Id)) }
    if ($Name) { $conditions.Add([System.Windows.Automation.PropertyCondition]::new($elementType::NameProperty, $Name)) }
    if ($Type) { $conditions.Add([System.Windows.Automation.PropertyCondition]::new($elementType::ControlTypeProperty, $Type)) }
    $conditions.Add([System.Windows.Automation.PropertyCondition]::new($elementType::IsOffscreenProperty, $false))
    $condition = if ($conditions.Count -eq 1) { $conditions[0] } else { [System.Windows.Automation.AndCondition]::new($conditions.ToArray()) }
    return $Parent.FindFirst($scope, $condition)
}
function Invoke-Control($Element) {
    if (-not $Element) { throw 'Required UIA control is missing' }
    $Element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
}
function Get-WindowClass([IntPtr]$Handle) {
    $value = [Text.StringBuilder]::new(256)
    [void][WorkspaceEditorNative]::GetClassName($Handle, $value, 256)
    return $value.ToString()
}
function Test-OwnedWindow([IntPtr]$Handle) {
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
function Send-Text([string]$Text, [switch]$Editor) {
    foreach ($character in $Text.ToCharArray()) {
        Assert-InputTarget -Editor:$Editor
        [WorkspaceEditorNative]::Unicode($character)
    }
}
function Focus-Editor {
    if (-not [WorkspaceEditorNative]::ActivateWindow($script:testHandle)) { throw 'Cannot activate dedicated test window' }
    $editor = Find-Control $script:webRoot -Name '工作区文件编辑器'
    if (-not $editor) { throw 'Monaco accessible textarea is missing' }
    $editor.SetFocus()
    Start-Sleep -Milliseconds 150
    Wait-Check { $elementType::FocusedElement.Current.Name -eq '工作区文件编辑器' } 'Monaco has keyboard focus'
    Assert-InputTarget -Editor
}
function Replace-EditorText([string]$Text) {
    Focus-Editor
    Send-Chord @(0x11, 0x41) -Editor
    Send-Text $Text -Editor
    Start-Sleep -Milliseconds 200
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
    Wait-Check { [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } "Native editor displays $Name"
    Wait-Check {
        $names = $script:webRoot.FindAll($scope, [System.Windows.Automation.Condition]::TrueCondition)
        @($names | Where-Object { $_.Current.Name -like "*$Name*" }).Count -gt 0
    } "Bridge activates $Name"
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
    $script:testRoot = $elementType::FromHandle($script:testHandle)
    Wait-Check { (Get-ProbeLog) -match 'ready: workspace document bridge' } 'Native document bridge is ready'
    $children = [Collections.Generic.List[IntPtr]]::new()
    [void][WorkspaceEditorNative]::EnumChildWindows($script:testHandle, { param($child, $state) $children.Add($child); return $true }, [IntPtr]::Zero)
    foreach ($child in $children) {
        $title = [Text.StringBuilder]::new(256)
        [void][WorkspaceEditorNative]::GetWindowText($child, $title, 256)
        if ($title.ToString() -eq 'Sansterminal EditorHostProbe') { $script:surfaceHandle = $child }
    }
    foreach ($child in $children) {
        if ((Get-WindowClass $child) -eq 'Chrome_WidgetWin_0' -and [WorkspaceEditorNative]::IsChild($script:surfaceHandle, $child)) {
            $script:webRoot = $elementType::FromHandle($child)
            break
        }
    }
    Assert-Check ($script:surfaceHandle -ne [IntPtr]::Zero -and $null -ne $script:webRoot) 'Native WebView2 surface exists in the dedicated window'
    Wait-Check { (Get-ProbeLog) -match 'smoke-ok:.*bundled command undo preserved' } 'Offline models, worker services and bundled extension undo pass self-test'
    $browserMatch = [regex]::Match((Get-ProbeLog), 'browser-process: (\d+)')
    Assert-Check $browserMatch.Success 'Owned browser process is recorded'
    $browserProcessId = [int]$browserMatch.Groups[1].Value
    $browser = Get-CimInstance Win32_Process -Filter "ProcessId = $browserProcessId"
    Assert-Check ($browser.ParentProcessId -eq $ProbeProcessId) 'Recorded browser process was created by the dedicated target process'
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
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'dirty-alpha' } 'Dirty text survives file and workspace switches'
    Focus-Editor
    Send-Chord @(0x11, 0x5A) -Editor
    Start-Sleep -Milliseconds 200
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'saved-alpha' } 'Original edit history survives workspace switching'
    Focus-Editor
    Send-Chord @(0x11, 0x59) -Editor
    Start-Sleep -Milliseconds 200
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'dirty-alpha' } 'Redo restores the pre-switch edit'
    Focus-Editor
    Send-Chord @(0x11, 0x41) -Editor
    Wait-Check { (Find-Control $script:webRoot -Name '选区转大写' -Type ([System.Windows.Automation.ControlType]::Button)).Current.IsEnabled } 'Bundled extension command is enabled for the selection'
    Invoke-Control (Find-Control $script:webRoot -Name '选区转大写' -Type ([System.Windows.Automation.ControlType]::Button))
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'DIRTY-ALPHA' } 'Bundled extension edits and saves the active real document'
    Focus-Editor
    Send-Chord @(0x11, 0x5A) -Editor
    Start-Sleep -Milliseconds 200
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText($alphaFile) -eq 'dirty-alpha' } 'Extension edit participates in Monaco undo'

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
    Wait-Check { $null -ne (Get-UnsavedDialog) } 'Second close still detects unsaved changes'
    Invoke-UnsavedChoice '放弃'
    Wait-Check { $null -eq (Get-DocumentTab 'conflict.txt') } 'Discard closes the selected dirty document'
    Assert-Check ([IO.File]::ReadAllText($conflictFile) -eq 'external-version') 'Discard preserves the external disk contents'

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
            $graphics.CopyFromScreen($capture.Left, $capture.Top, 0, 0, $bitmap.Size)
            $bitmap.Save([IO.Path]::GetFullPath($ScreenshotPath), [Drawing.Imaging.ImageFormat]::Png)
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
        Assert-Check (Test-Path -LiteralPath $ScreenshotPath -PathType Leaf) 'Dedicated window screenshot is saved before close'
    }
    if ($Close) {
        [void][WorkspaceEditorNative]::PostMessage($script:testHandle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
        # Multiple fixture workspaces may have created multiple terminals.
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while ([WorkspaceEditorNative]::IsWindow($script:testHandle) -and [DateTime]::UtcNow -lt $deadline) {
            $confirmation = Find-Control $script:testRoot -Id 'ConfirmCloseDialog'
            if ($confirmation) { Invoke-Control (Find-Control $confirmation -Id 'PrimaryButton') }
            if (Get-UnsavedDialog) { throw 'Unexpected dirty document remains at final close' }
            Start-Sleep -Milliseconds 100
        }
        Wait-Check { (Get-ProbeLog) -match 'closed: controller released' } 'Closing the dedicated window releases its controller'
        Wait-Check { -not (Get-Process -Id $browserProcessId -ErrorAction SilentlyContinue) } 'Owned browser process exits after window close'
    } else {
        [void][WorkspaceEditorNative]::SetWindowPos($script:testHandle, [IntPtr]::Zero, $originalBounds.Left, $originalBounds.Top, $originalBounds.Right - $originalBounds.Left, $originalBounds.Bottom - $originalBounds.Top, 0x14)
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
        [void][WorkspaceEditorNative]::SetWindowPos($script:testHandle, [IntPtr]::Zero, $originalBounds.Left, $originalBounds.Top, $originalBounds.Right - $originalBounds.Left, $originalBounds.Bottom - $originalBounds.Top, 0x14)
    }
    throw
}
