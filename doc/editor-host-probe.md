# 阶段 2：原生 Monaco 宿主验证

此验证在现有窗口中加入独立的 Win32 WebView2 区域，与 XAML 终端并排。它用于决定混合宿主路线能否采用，尚未替换工作区文件预览器，也不提供文件保存。编辑的是两个临时缓冲区，关闭窗口即丢弃。

## 构建与运行

普通构建默认不编译或打包此验证，不需要 Node.js。启用验证需要 Node.js 22+、npm、现有 C++ 构建环境，以及已安装的 WebView2 Runtime。

先在 `src/cascadia/EditorHostProbe` 运行：

```powershell
npm.cmd ci --ignore-scripts --no-audit --no-fund
npm.cmd run build
```

锁文件固定 Monaco 0.57.0、esbuild 0.28.2 及其依赖。`dist` 包含编辑器、worker、字体和许可证；运行时不使用 CDN。返回仓库根目录，在开发环境 PowerShell 中构建、部署：

```powershell
Import-Module .\tools\OpenConsole.psm1
Set-MsBuildDevEnvironment
msbuild OpenConsole.slnx /t:"Terminal\CascadiaPackage" /p:Platform=x64 /p:Configuration=Debug /p:EnableEditorHostProbe=true /m
# 先关闭正在运行的开发版终端。
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\DeployAppRecipe.exe" src\cascadia\CascadiaPackage\bin\x64\Debug\CascadiaPackage.build.appxrecipe
```

从正式执行别名激活开发包，并只为当前启动设置验证开关：

```powershell
$env:SANSTERMINAL_EDITOR_PROBE = '1'
try {
    & "$env:LOCALAPPDATA\Microsoft\WindowsApps\wtd.exe" -w new nt --title EditorHostProbe cmd.exe /d /k echo EditorHostProbe
} finally {
    Remove-Item Env:\SANSTERMINAL_EDITOR_PROBE
}
```

两个开关都需要：构建参数启用代码及资源，环境变量启用面板。未设置环境变量时保持常规终端界面。不要直接启动 AppX 文件夹中的 `WindowsTerminal.exe`；本机实测该方式无法正确激活开发包，会在未启用验证面板时同样中止。

## 验证内容

顶部的验证工具栏可隐藏／显示编辑器、聚焦编辑器、打开 XAML 对话框和运行自检。拖动终端与编辑器之间的分隔线改变宽度。网页内的 A/B 按钮在同一个 Editor 上切换两个 Model；各自保存视图状态，隐藏宿主也不销毁 Model。

自动自检使用独立临时 Model，不修改交互缓冲区：执行编辑后切换 100 次，检查文本、光标位置、撤销／重做和 Model 数量，再分别请求 JSON 校验与 TypeScript 语法诊断，确认 worker 实际响应。初次加载与点击“Self-test／运行自检”都会运行。

原生自动化脚本检查焦点进出、真实 XAML 模态框、10 次隐藏／恢复、窗口缩放、最大化／最小化、唯一 controller 和关闭后的浏览器进程退出。先确保是专门用于验证的可见窗口，每个测试进程只有一个窗口和一个测试终端：

```powershell
# 使用上述窗口的 WindowsTerminal 进程 ID，而不是短暂的 wtd 启动器 ID。
$probeProcess = Get-Process WindowsTerminal | Where-Object MainWindowTitle -eq 'EditorHostProbe'
$probeLog = Get-ChildItem "$env:LOCALAPPDATA\Packages\WindowsTerminalDev_8wekyb3d8bbwe\LocalCache\Sansterminal\EditorHostProbe" -Recurse -Filter probe.log |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
.\tools\Test-EditorHostProbe.ps1 -ProbeProcessId $probeProcess.Id -LogPath $probeLog.FullName -Close
```

`-Close` 会关闭指定的测试窗口，因此不要在其中运行真实任务。脚本不操作其他终端窗口。窗口关闭后检查日志的 `closed: controller released`，以及该日志中记录的 browser PID 退出。缓存保留在各自 GUID 子目录，便于排查；它不是文档恢复数据。

## 宿主边界

- 每个启用验证的窗口有一个 controller。异步创建使用弱引用，关闭后迟到的 controller 会立即关闭；定时器、事件和原生 HWND 随窗口释放。
- XAML 占位区域负责布局；宿主按 XamlRoot 的缩放及 Island 到父窗口的坐标转换设置原生矩形。正常终端连接和分屏仍由现有代码管理。
- 原生浏览器拥有焦点时，`AppHost` 不向残留的 XAML 焦点目标发送终端快捷键。返回终端需要先通过 `DesktopWindowXamlSource.NavigateFocus` 进入 Island，再聚焦终端；焦点交接延迟到浏览器回调返回之后执行。
- XAML `ContentDialog` 打开前发出可见性事件，暂时隐藏原生编辑器，关闭后恢复同一个 controller。其他 XAML popup 由布局检查及 50 ms 验证定时器检测。这是需要评估的限制：模态框期间编辑器背景暂时不可见，普通 popup 协调尚不是生产级方案。
- 只映射应用自己的 `EditorHostProbe` 资源目录到固定虚拟 HTTPS 来源。导航、子框架、新窗口、下载、宿主对象和权限受限；消息验证完整来源、类型和长度。桥接只接受聚焦、测试对话框和诊断消息，没有读写项目文件或执行命令的接口。
- WebView2 Runtime 缺失、离线资源缺失或浏览器故障时显示错误，保留终端。自动恢复浏览器及保存真实文件不在本验证范围内。

本地资源映射采用 [WebView2 虚拟主机映射](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/working-with-local-content)，消息来源检查遵循 [WebView2 安全建议](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security)。Monaco 使用 [ESM 方式](https://github.com/microsoft/monaco-editor/blob/main/docs/integrate-esm.md)打包，实际导入路径以锁定版本的 `exports` 为准。

## 验收记录

2026-09-29，`x64 Debug`、WebView2 Runtime `154.0.4258.37` 下已完成：离线前端构建、验证包构建及 `DeployAppRecipe` 部署；正式执行别名启动；同窗终端与 Monaco；100 次模型切换和 JSON／TypeScript worker 自检。

`Test-EditorHostProbe.ps1 -Close` 的 43 项检查全部通过：焦点返回真实 `TermControl`、编辑器焦点恢复、真实 XAML 模态框、10 次隐藏／恢复、900／1400／1100 像素宽度、最大化／最小化、重复 Model／worker 自检、全过程一个 controller、无运行错误、关闭后 controller 释放及独立 browser PID 退出。原有 TAEF `*Tab*Layout*` 的 3 个用例也通过。

`EnableEditorHostProbe=false` 的普通 `x64 Debug` 包构建通过，检查 MSIX 后确认未包含 `EditorHostProbe` 资源。已部署的验证版本在不设置环境变量时也没有创建浏览器用户目录。构建仍有现有 PRI263 资源警告。

验证修正了两个实际问题：原生焦点不能只依靠 XAML 控件的 `Focus()`；浏览器自动布局在连续原生缩放时会触发 ResizeObserver 循环，现改为下一帧更新布局。模态框后的焦点恢复在 XAML 完成恢复流程后执行。

本机只有一块显示器，跨屏 DPI 未验证。以下项目必须继续实测，不能由上述自检代替：

| 项目 | 验证方式与通过条件 |
| --- | --- |
| 中文输入法 | 拼音组合、候选框、确认／取消、连续输入、撤销正常；候选框跟随光标，无字符进入终端 |
| 快捷键与剪贴板 | 编辑器中的 `Ctrl+C/F/S/Z` 不触发终端操作；复制粘贴和右键菜单可用；F6 返回终端后输入正确 |
| DPI | 在不同 DPI 显示器间移动；拖动分隔线和恢复窗口后，无空隙、覆盖、输入偏移或候选框漂移 |
| 全屏与专注 | 切换窗口模式后标题栏及编辑器边界正确，焦点可返回；确认顶部标签模式与左侧标签模式 |
| XAML popup | 菜单、命令面板、搜索等浮层可操作；评估暂时隐藏编辑器和轮询延迟是否可以接受 |
| 长时间稳定性 | 多次打开／关闭窗口、长任务输出和持续切换后，终端 PID 不变，宿主及浏览器资源不持续增长 |

阶段 2 仍是技术验证。只有这些门槛通过并接受混合窗口的遮挡约束后，才进入正式文档服务和 UI 迁移。
