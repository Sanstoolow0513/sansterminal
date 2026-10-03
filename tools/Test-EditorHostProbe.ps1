# Run against a dedicated, visible editor probe window, with one terminal.
# The optional -Close switch closes that test window after the checks.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][int]$ProbeProcessId,
    [Parameter(Mandatory)][string]$LogPath,
    [switch]$Close
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class EditorProbeNative {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr state);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr state);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hwnd, EnumProc callback, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder value, int size);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder value, int size);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
}
'@
function Assert-Probe($Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    Write-Output "PASS: $Message"
}
function Wait-Probe([scriptblock]$Condition, [string]$Message) {
    $deadline = (Get-Date).AddSeconds(10)
    while (!( & $Condition )) {
        if ((Get-Date) -gt $deadline) { throw "Timeout: $Message" }
        Start-Sleep -Milliseconds 100
    }
    Write-Output "PASS: $Message"
}
function Find-Named($Parent, [string]$Name) {
    $Parent.FindFirst([System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty, $Name))
}
function Invoke-Named($Parent, [string]$Name) {
    $element = Find-Named $Parent $Name
    if (!$element) { throw "Missing probe control: $Name" }
    $element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
}
function Get-Rect([IntPtr]$Handle) {
    $rect = [EditorProbeNative+Rect]::new()
    [void][EditorProbeNative]::GetWindowRect($Handle, [ref]$rect)
    $rect
}

$windows = [Collections.Generic.List[IntPtr]]::new()
[void][EditorProbeNative]::EnumWindows({ param($handle, $state)
    [uint32]$owner = 0
    [void][EditorProbeNative]::GetWindowThreadProcessId($handle, [ref]$owner)
    $class = [Text.StringBuilder]::new(256)
    [void][EditorProbeNative]::GetClassName($handle, $class, 256)
    if ($owner -eq $ProbeProcessId -and $class.ToString() -eq 'CASCADIA_HOSTING_WINDOW_CLASS') { $windows.Add($handle) }
    return $true
}, [IntPtr]::Zero)
Assert-Probe ($windows.Count -eq 1) 'Exactly one test window belongs to the supplied process'
$handle = $windows[0]
$root = [System.Windows.Automation.AutomationElement]::FromHandle($handle)
Assert-Probe ($null -ne (Find-Named $root 'Editor probe: show/hide')) 'Target is an editor probe'
$children = [Collections.Generic.List[IntPtr]]::new()
[void][EditorProbeNative]::EnumChildWindows($handle, { param($child, $state) $children.Add($child); return $true }, [IntPtr]::Zero)
$surface = [IntPtr]::Zero
$web = $null
foreach ($child in $children) {
    $name = [Text.StringBuilder]::new(256)
    [void][EditorProbeNative]::GetWindowText($child, $name, 256)
    if ($name.ToString() -eq 'Sansterminal EditorHostProbe') { $surface = $child }
    $class = [Text.StringBuilder]::new(256)
    [void][EditorProbeNative]::GetClassName($child, $class, 256)
    if ($class.ToString() -eq 'Chrome_WidgetWin_0') { $web = [System.Windows.Automation.AutomationElement]::FromHandle($child) }
}
Assert-Probe ($surface -ne [IntPtr]::Zero -and $null -ne $web) 'Native WebView2 surface exists'
Wait-Probe { (Get-Content -LiteralPath $LogPath -Raw) -match 'smoke-ok:' } 'Offline model and worker self-test completed'
$browserProcessId = [int]([regex]::Match((Get-Content -LiteralPath $LogPath -Raw), 'browser-process: (\d+)').Groups[1].Value)
Assert-Probe ($browserProcessId -gt 0) 'Browser process is recorded'

Invoke-Named $root 'Focus editor'
Wait-Probe { [System.Windows.Automation.AutomationElement]::FocusedElement.Current.ProcessId -eq $browserProcessId } 'Browser receives native keyboard focus'
Invoke-Named $web '终端（F6）'
Wait-Probe {
    $focus = [System.Windows.Automation.AutomationElement]::FocusedElement.Current
    $focus.ProcessId -eq $ProbeProcessId -and $focus.ClassName -eq 'TermControl'
} 'Focus returns from browser to XAML terminal'
Invoke-Named $root 'Focus editor'
Wait-Probe { [System.Windows.Automation.AutomationElement]::FocusedElement.Current.ProcessId -eq $browserProcessId } 'Editor can regain focus'
Invoke-Named $web 'XAML 对话框'
Wait-Probe { $null -ne (Find-Named $root 'Return to editor') } 'Real XAML modal opens'
Assert-Probe (![EditorProbeNative]::IsWindowVisible($surface)) 'Native surface yields to modal'
Invoke-Named $root 'Return to editor'
Wait-Probe { [EditorProbeNative]::IsWindowVisible($surface) } 'Same native surface returns after modal'
Wait-Probe { [System.Windows.Automation.AutomationElement]::FocusedElement.Current.ProcessId -eq $browserProcessId } 'Modal restores editor focus'

for ($i = 0; $i -lt 10; $i++) {
    Invoke-Named $root 'Editor probe: show/hide'
    Wait-Probe { ![EditorProbeNative]::IsWindowVisible($surface) } 'Hide editor'
    Invoke-Named $root 'Editor probe: show/hide'
    Wait-Probe { [EditorProbeNative]::IsWindowVisible($surface) } 'Restore editor'
}
$original = Get-Rect $handle
try {
    foreach ($width in @(900, 1400, 1100)) {
        [void][EditorProbeNative]::SetWindowPos($handle, [IntPtr]::Zero, $original.Left, $original.Top, $width, 650, 0x14)
        Start-Sleep -Milliseconds 200
        $outer = Get-Rect $handle
        $inner = Get-Rect $surface
        Assert-Probe ($inner.Left -ge $outer.Left -and $inner.Right -le $outer.Right -and $inner.Bottom -le $outer.Bottom -and $inner.Top -gt $outer.Top + 30) "Native bounds stay below titlebar and inside resized window ($width)"
    }
    $pattern = $root.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern)
    $pattern.SetWindowVisualState([System.Windows.Automation.WindowVisualState]::Maximized)
    Start-Sleep -Milliseconds 250
    $outer = Get-Rect $handle
    $inner = Get-Rect $surface
    Assert-Probe ($inner.Right -le $outer.Right -and $inner.Bottom -le $outer.Bottom) 'Maximized native surface fits window'
    $pattern.SetWindowVisualState([System.Windows.Automation.WindowVisualState]::Minimized)
    Wait-Probe { ![EditorProbeNative]::IsWindowVisible($surface) } 'Minimizing hides editor surface'
    $pattern.SetWindowVisualState([System.Windows.Automation.WindowVisualState]::Normal)
    Wait-Probe { [EditorProbeNative]::IsWindowVisible($surface) } 'Restore makes editor surface visible'
} finally {
    [void][EditorProbeNative]::SetWindowPos($handle, [IntPtr]::Zero, $original.Left, $original.Top, $original.Right - $original.Left, $original.Bottom - $original.Top, 0x14)
}
$oldPasses = ([regex]::Matches((Get-Content -LiteralPath $LogPath -Raw), 'smoke-ok:')).Count
Invoke-Named $root 'Self-test'
Wait-Probe { ([regex]::Matches((Get-Content -LiteralPath $LogPath -Raw), 'smoke-ok:')).Count -gt $oldPasses } 'Models and workers still work after native layout changes'
$log = Get-Content -LiteralPath $LogPath -Raw
Assert-Probe (([regex]::Matches($log, 'controller-created:')).Count -eq 1) 'One controller across all switches and visibility changes'
Assert-Probe ($log -notmatch 'smoke-failed:|host-error:|page-error:') 'No reported runtime errors'
if ($Close) {
    [void][EditorProbeNative]::PostMessage($handle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    Wait-Probe { (Get-Content -LiteralPath $LogPath -Raw) -match 'closed: controller released' } 'Closing window releases controller'
    Wait-Probe { !(Get-Process -Id $browserProcessId -ErrorAction SilentlyContinue) } 'Owned browser process exits'
}
