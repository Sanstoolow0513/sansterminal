# Copyright (c) Sansterminal contributors. Licensed under the MIT license.
# Requires a dedicated portable test process starting on empty Home. Its profile
# must bind Ctrl+Alt+L/Backspace/Q/N to layout settings/closeTab/quit/newWindow,
# disable close warnings, and enable persistedLayoutAndContent.
# Real NTFS oplocks hold native reads/saves pending without production test hooks.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][int]$ProbeProcessId,
    [Parameter(Mandatory)][string]$LogPath,
    [Parameter(Mandatory)][string]$WorkspacePath,
    [Parameter(Mandatory)][string]$SettingsPath
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Test-WorkspaceEditor.ps1') -ProbeProcessId $ProbeProcessId -LogPath $LogPath -WorkspacePath $WorkspacePath -SettingsPath $SettingsPath -LibraryOnly
if (Test-Path -LiteralPath $fixture) { throw 'Lifecycle fixtures must use a new directory' }
[void][IO.Directory]::CreateDirectory($fixture)
$script:fixtureCreated = $true
Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public sealed class WorkspaceIoGate : IDisposable {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateFile(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool DeviceIoControl(IntPtr file, uint code, IntPtr input, uint inputSize, IntPtr output, uint outputSize, out uint returned, IntPtr overlapped);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr CreateEvent(IntPtr security, bool manual, bool initial, string name);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    IntPtr file, signal, overlapped;
    public WorkspaceIoGate(string path) {
        file = CreateFile(path, 0x80000000, 7, IntPtr.Zero, 3, 0x40000000, IntPtr.Zero);
        if (file == new IntPtr(-1)) throw new Win32Exception();
        signal = CreateEvent(IntPtr.Zero, true, false, null);
        overlapped = Marshal.AllocHGlobal(IntPtr.Size == 8 ? 32 : 20);
        Marshal.Copy(new byte[IntPtr.Size == 8 ? 32 : 20], 0, overlapped, IntPtr.Size == 8 ? 32 : 20);
        Marshal.WriteIntPtr(overlapped, IntPtr.Size == 8 ? 24 : 16, signal);
        uint returned;
        if (!DeviceIoControl(file, 0x90000, IntPtr.Zero, 0, IntPtr.Zero, 0, out returned, overlapped) && Marshal.GetLastWin32Error() != 997) {
            var error = new Win32Exception(); Dispose(); throw error;
        }
    }
    public bool Waiting { get { return WaitForSingleObject(signal, 0) == 0; } }
    public void Dispose() {
        if (file != IntPtr.Zero) { CloseHandle(file); file = IntPtr.Zero; }
        if (signal != IntPtr.Zero) { WaitForSingleObject(signal, 5000); CloseHandle(signal); signal = IntPtr.Zero; }
        if (overlapped != IntPtr.Zero) { Marshal.FreeHGlobal(overlapped); overlapped = IntPtr.Zero; }
    }
}
'@
function Get-TestWindows {
    $windows = [Collections.Generic.List[IntPtr]]::new()
    [void][WorkspaceEditorNative]::EnumWindows({ param($candidate, $state)
        [uint32]$ownerProcess = 0
        [void][WorkspaceEditorNative]::GetWindowThreadProcessId($candidate, [ref]$ownerProcess)
        if ($ownerProcess -eq $ProbeProcessId -and (Get-WindowClass $candidate) -eq 'CASCADIA_HOSTING_WINDOW_CLASS') { $windows.Add($candidate) }
        return $true
    }, [IntPtr]::Zero)
    return $windows.ToArray()
}
function Select-PendingFile([string]$Name) {
    $item = Find-Control (Get-FileTree) -Name $Name
    while ($item -and $item.Current.ControlType -ne [System.Windows.Automation.ControlType]::TreeItem) {
        $item = [System.Windows.Automation.TreeWalker]::RawViewWalker.GetParent($item)
    }
    if (-not $item) { throw "Missing tree item: $Name" }
    $item.SetFocus()
    Send-Chord @(0x0D)
}
function Test-EditorFocused {
    $focused = $elementType::FocusedElement.Current
    return $focused.Name -eq '工作区文件编辑器' -and $focused.HasKeyboardFocus
}
try {
    Wait-Check { @(Get-TestWindows).Count -eq 1 } 'One dedicated Home window is running'
    $script:testHandle = @(Get-TestWindows)[0]
    [void][WorkspaceEditorNative]::ActivateWindow($script:testHandle)
    Connect-XamlRoot
    Wait-Check { $null -ne (Find-Control $script:testRoot -Id 'WorkspaceHubTitle') } 'Test starts on Home'
    Open-LayoutSettings
    Open-LayoutSettings
    Close-LayoutSettings
    Wait-Check { $null -ne (Find-Control $script:testRoot -Id 'WorkspaceHubTitle') } 'Reusing Settings preserves the empty Home return route'
    Assert-Check (-not (Find-Control $script:testRoot -Id 'WorkspaceHubBackButton').Current.IsEnabled) 'Temporary Settings workspace is removed'

    $project = Join-Path $fixture 'project'
    [void][IO.Directory]::CreateDirectory($project)
    foreach ($name in @('alpha.txt', 'beta.txt', 'gamma.txt')) { [IO.File]::WriteAllText((Join-Path $project $name), "original-$name") }
    Open-WorkspaceFolder $project
    Open-TreeFile 'alpha.txt'
    Wait-Check { Test-EditorFocused } 'First file receives editor focus without an explicit editor click'
    Replace-EditorText 'dirty-alpha'
    $gate = [WorkspaceIoGate]::new((Join-Path $project 'beta.txt'))
    try {
        Select-PendingFile 'beta.txt'
        Wait-Check { $gate.Waiting } 'Native beta read is held pending'
        Wait-Check { [WorkspaceEditorNative]::IsChild($script:surfaceHandle, [WorkspaceEditorNative]::FocusedWindow()) } 'Pending file owns the editor surface focus'
        Send-Text 'must-not-edit-alpha'
        Send-Chord @(0x11, 0x53)
        Start-Sleep -Milliseconds 250
        Assert-Check ([IO.File]::ReadAllText((Join-Path $project 'alpha.txt')) -eq 'original-alpha.txt') 'Ctrl+S during beta load cannot save alpha'
        Send-Chord @(0x75)
        Wait-Check { -not [WorkspaceEditorNative]::IsChild($script:surfaceHandle, [WorkspaceEditorNative]::FocusedWindow()) } 'F6 transfers focus to the terminal while the read is pending'
        Send-Text 'echo LIFECYCLE_FIRST_WINDOW'
        Send-Chord @(0x0D)
    } finally { $gate.Dispose() }
    Wait-Check {
        $names = $script:webRoot.FindAll($scope, [System.Windows.Automation.Condition]::TrueCondition)
        @($names | Where-Object { $_.Current.Name -like '*beta.txt*' }).Count -gt 0
    } 'Beta activates when its read completes'
    Start-Sleep -Milliseconds 250
    Assert-Check (-not (Test-EditorFocused)) 'Read completion preserves terminal focus'
    Open-TreeFile 'alpha.txt'
    Save-Editor
    Wait-Check { [IO.File]::ReadAllText((Join-Path $project 'alpha.txt')) -eq 'dirty-alpha' } 'Typing during beta load leaves alpha buffer unchanged'

    $gate = [WorkspaceIoGate]::new((Join-Path $project 'alpha.txt'))
    try {
        Replace-EditorText 'save-one'
        Save-Editor
        Wait-Check { $gate.Waiting } 'First native save is held pending'
        Replace-EditorText 'save-two'
        Save-Editor
        Replace-EditorText 'save-three'
        Save-Editor
    } finally { $gate.Dispose() }
    Wait-Check { [IO.File]::ReadAllText((Join-Path $project 'alpha.txt')) -eq 'save-three' } 'Queued saves leave the newest snapshot on disk'
    Start-Sleep -Milliseconds 500
    Assert-Check ([IO.File]::ReadAllText((Join-Path $project 'alpha.txt')) -eq 'save-three') 'An older waiting snapshot cannot overwrite the newest save'

    Focus-Editor
    Send-Chord @(0x11, 0x10, 0x50) -Editor
    Wait-Check { -not [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } 'Command palette suppresses the native editor'
    Wait-Check { $elementType::FocusedElement.Current.ProcessId -eq $ProbeProcessId } 'Command palette receives XAML keyboard focus'
    Send-Chord @(0x1B)
    Wait-Check { [WorkspaceEditorNative]::IsWindowVisible($script:surfaceHandle) } 'Dismissing the palette restores the native editor'

    Replace-EditorText 'quit-confirmation-unsaved'
    $firstWindow = $script:testHandle
    # The fixture profile supplies these bindings; both windows must belong to
    # this exact process so no installed/user terminal is touched.
    Send-Chord @(0x75)
    Wait-Check { -not (Test-EditorFocused) } 'Window commands are sent from the native terminal'
    Send-Chord @(0x11, 0x12, 0x4E)
    Wait-Check { @(Get-TestWindows).Count -eq 2 } 'A second window belongs to the dedicated process'
    $script:testHandle = @(Get-TestWindows) | Where-Object { $_ -ne $script:testHandle } | Select-Object -First 1
    [void][WorkspaceEditorNative]::ActivateWindow($script:testHandle)
    Connect-XamlRoot
    Wait-Check { $null -ne (Get-Terminal) } 'Second window has a terminal buffer'
    (Get-Terminal).SetFocus()
    Send-Text 'echo LIFECYCLE_SECOND_WINDOW'
    Send-Chord @(0x0D)
    Start-Sleep -Milliseconds 250
    $executable = (Get-Process -Id $ProbeProcessId).Path
    $secondWindow = $script:testHandle
    foreach ($showCommand in @(6, 0)) { # SW_MINIMIZE, SW_HIDE
        [void][WorkspaceEditorNative]::ShowWindow($firstWindow, $showCommand)
        [void][WorkspaceEditorNative]::ActivateWindow($secondWindow)
        Send-Chord @(0x11, 0x12, 0x51)
        # Do not activate the first window here: Quit must reveal it itself.
        Wait-Check { [WorkspaceEditorNative]::GetForegroundWindow() -eq $firstWindow -and [WorkspaceEditorNative]::IsWindowVisible($firstWindow) } "Quit reveals the earlier window (ShowWindow=$showCommand)"
        $script:testHandle = $firstWindow
        Connect-XamlRoot
        Wait-Check { $null -ne (Get-UnsavedDialog) } 'Revealed window exposes its unsaved-document confirmation'
        Invoke-UnsavedChoice '取消'
        Wait-Check { $null -eq (Get-UnsavedDialog) } 'Cancel dismisses the confirmation'
        Assert-Check (@(Get-TestWindows).Count -eq 2) 'Cancel preserves both windows and allows another Quit'
        $script:testHandle = $secondWindow
        Connect-XamlRoot
    }
    [void][WorkspaceEditorNative]::ShowWindow($firstWindow, 6)
    [void][WorkspaceEditorNative]::ActivateWindow($secondWindow)
    Send-Chord @(0x11, 0x12, 0x51)
    Wait-Check { [WorkspaceEditorNative]::GetForegroundWindow() -eq $firstWindow } 'Retrying Quit activates the confirmation owner'
    $script:testHandle = $firstWindow
    Connect-XamlRoot
    Wait-Check { $null -ne (Get-UnsavedDialog) } 'Retried Quit still confirms unsaved changes'
    Invoke-UnsavedChoice '放弃'
    Wait-Check { -not (Get-Process -Id $ProbeProcessId -ErrorAction SilentlyContinue) } 'Quit exits the dedicated process'
    $settingsDirectory = Split-Path -Parent $SettingsPath
    $state = Get-Content -Raw (Join-Path $settingsDirectory 'state.json') | ConvertFrom-Json
    Assert-Check (@($state.persistedWindowLayouts).Count -eq 2) 'Quit persists both window layouts'
    Assert-Check (@(Get-ChildItem -LiteralPath $settingsDirectory -Filter 'buffer_*.txt').Count -ge 2) 'Quit persists terminal content for both windows'
    $buffers = Get-ChildItem -LiteralPath $settingsDirectory -Filter 'buffer_*.txt' | Get-Content -Raw
    Assert-Check (($buffers -match 'LIFECYCLE_FIRST_WINDOW').Count -gt 0 -and ($buffers -match 'LIFECYCLE_SECOND_WINDOW').Count -gt 0) 'Both terminal markers survive session persistence'
    $restarted = Start-Process -FilePath $executable -WindowStyle Hidden -PassThru
    $ProbeProcessId = $restarted.Id
    Wait-Check { @(Get-TestWindows).Count -eq 2 } 'Restart restores both windows'
    $script:testHandle = @(Get-TestWindows)[0]
    [void][WorkspaceEditorNative]::ActivateWindow($script:testHandle)
    Connect-XamlRoot
    (Get-Terminal).SetFocus()
    Send-Chord @(0x11, 0x12, 0x51)
    Wait-Check { -not (Get-Process -Id $ProbeProcessId -ErrorAction SilentlyContinue) } 'Restored test windows close cleanly'
} finally {
    Write-Diagnostics
    $results | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $fixture 'results.json') -Encoding utf8
    Write-Output "Lifecycle evidence: $fixture"
}
