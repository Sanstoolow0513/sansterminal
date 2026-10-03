# WebView 工作区编辑器验证

本验证分支 `verify/webview-editor-integration` 基于 `dev`。目标是验证能否用离线 Monaco 替换现有文件预览器，并让编辑器复用 Terminal 的文件树、文档标签、工作区、焦点和窗口生命周期。此前的 [Monaco 宿主验证](editor-host-probe.md) 保留为独立的临时缓冲区测试。

## 启用

需要现有 C++ 构建环境、Node.js 22+ 和 WebView2 Runtime。前端使用现有锁文件，不访问 CDN。

```powershell
Push-Location src/cascadia/EditorHostProbe
npm.cmd ci --ignore-scripts --no-audit --no-fund
npm.cmd test
npm.cmd run build
Pop-Location

Import-Module .\tools\OpenConsole.psm1
Set-MsBuildDevEnvironment
msbuild OpenConsole.slnx /t:"Terminal\CascadiaPackage" /p:Platform=x64 /p:Configuration=Debug /p:EnableEditorHostProbe=true /p:CL_MPCount=2 /m:1
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\DeployAppRecipe.exe" src\cascadia\CascadiaPackage\bin\x64\Debug\CascadiaPackage.build.appxrecipe

$env:SANSTERMINAL_WORKSPACE_EDITOR = '1'
try {
    & "$env:LOCALAPPDATA\Microsoft\WindowsApps\wtd.exe" -w new nt --title WebViewEditorValidation cmd.exe /d /k echo WebViewEditorValidation
} finally {
    Remove-Item Env:\SANSTERMINAL_WORKSPACE_EDITOR
}
```

部署前关闭开发包窗口。商店版 Windows Terminal 不受此开关影响。构建开关与当前启动的环境变量都必须启用；只有 `SANSTERMINAL_EDITOR_PROBE=1` 时继续运行原临时缓冲区验证。两个环境变量同时启用时优先使用工作区编辑器。

工作区文件树目前位于左侧标签布局，先在设置中选择左侧标签并重启。打开文件夹后，点击文件使用原文档区域；不会添加第二个编辑器侧栏。`Ctrl+S` 保存，`F6` 返回终端。

## 集成职责

| 层 | 职责 |
| --- | --- |
| TerminalPage | 文档身份、已授权路径、原始磁盘内容、当前缓冲区、编码、未保存状态、保存冲突和关闭提示；复用文件树、标签和工作区布局 |
| TerminalWindow | 向窗口宿主提供编辑区域和消息事件 |
| AppHost / EditorHostProbe | 每窗口一个原生 HWND / WebView2 controller、离线资源、来源检查、JSON 消息传输、原生焦点、弹层和释放 |
| Monaco | 每文档一个 Model、视图位置、撤销历史、语言 worker，以及内置扩展命令 |

桥接协议版本为 `1`。原生端发送 `open / activate / close / saved / error`；前端发送 `ready / changed / save`。关闭前使用 `flush / flushed / resume` 同步最新缓冲区并暂时冻结编辑，超时取消关闭。文档消息使用原生端分配的身份；网页没有按任意路径读写文件或执行 shell 命令的接口。消息来源必须完全匹配应用的虚拟 HTTPS 页面，且有类型和长度限制。资源映射仅指向随应用打包的编辑器目录。

正常切换文档、工作区或临时隐藏编辑器都复用 Model。验证模式中，每个打开的文档标签都保留到显式关闭，修改后显示 `*`；这是为了避免文件树点击先于跨进程编辑消息到达时替换预览并丢失文字。默认只读预览模式仍保留临时预览标签行为。保存失败保留缓冲区；关闭文档、工作区或窗口时处理未保存内容。编辑器进程故障后可以重新加载，并恢复当前窗口原生端已收到的文字和未保存状态；此恢复会重置 Monaco 的撤销历史。应用进程退出后的文档恢复尚未实现。验证模式中的“退出所有窗口”暂时收敛为关闭当前窗口，避免跳过其他窗口的未保存提示；跨窗口退出协调仍待实现。

## 文件与保存边界

编辑验证面向不超过 2 MiB 的严格 UTF-8 和带 BOM 的 UTF-16 LE 文本，保留 BOM 和 LF／CRLF 换行。不支持的文本、二进制、混合／CR 换行及超限文件进入只读状态，没有截断内容回写路径。编辑缓冲区通信上限为 3 Mi 个 UTF-16 代码单元，超过后暂停同步和关闭，需缩减内容；保存后的文件仍须满足 2 MiB 字节上限。

保存前重新比较原始磁盘字节，发现外部修改则拒绝覆盖并保留当前缓冲区。写入使用同目录临时文件和原子替换，替换前再次比对路径内容。这不是文件系统提供的原子比较并交换，其他程序恰在最终比对后替换路径的竞态仍存在。当前没有文件监听、自动合并、另存为或跨窗口共享文档服务；同一文件在其他窗口打开时，保存冲突检查仍是必要边界。

文件读取和保存仍在 UI 线程，2 MiB 上限用于控制本次验证范围。每次内容变化向原生端发送完整文本；正式实现应改为异步文档服务及增量通信，并保留关闭前同步缓冲区的保证。

## 扩展性判断

本验证提供随应用打包的可信命令注册接口，通过编辑器操作修改 Model，保留撤销记录。它验证自有编辑器功能可以接入，不代表完成插件生态。

[Monaco 官方说明](https://github.com/microsoft/monaco-editor/blob/main/README.md)明确，VS Code 扩展不能直接运行在 Monaco。当前锁定版本提供补全、hover、格式化、定义、重命名、代码操作和诊断 API，可继续接入语言服务；LSP 的连接和服务端需要单独实现。兼容 VS Code 扩展还需 [extension host](https://code.visualstudio.com/api/advanced-topics/extension-host)、相应运行时和工作台 API。

当前使用 windowed WebView2，XAML 对话框打开时隐藏原生区域，关闭后恢复同一 controller；普通 popup 仍由 50 ms 轮询协调。若要完善层叠能力，可继续验证 [composition hosting](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/windowed-vs-visual-hosting)，同时承担输入转发与 DPI 协调。网页、对话框和焦点回调遵循 [WebView2 线程模型](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model)，桥接遵循其 [安全建议](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security)。

目前 Model URI 包含工作区、文档身份和文件名，没有完整源目录树；JSON、TypeScript、CSS 和 HTML worker 已打包，但跨文件 TypeScript import 解析和整个项目的语义服务尚未验证。

## 验收

2026-10-02，`x64 Debug`、WebView2 Runtime `154.0.4258.53` 下完成以下验收。人工验收还需覆盖中文输入法候选框、跨屏 DPI、全屏／专注模式、右键／剪贴板，以及长时间资源占用。

| 验证 | 实际结果 |
| --- | --- |
| 前端单测 | 17 项通过；包含模型／视图保留、dirty 与保存确认、关闭同步、过限保护、扩展撤销和错误按文档隔离 |
| 真实 Chromium | 离线 Monaco、JSON／TS／CSS／HTML worker、100 次模型切换、扩展命令与撤销、保存确认、关闭同步、亮暗主题均通过；无页面错误 |
| 生产文件读写代码 | UTF-8／UTF-16 LE 编码与 BOM／LF／CRLF 往返、非法文本／二进制／超限拒绝、实际原子保存、外部冲突、替换失败保留原文件、临时文件清理通过 |
| 工作区桌面验收 | `Test-WorkspaceEditor.ps1 -Close` 81 项通过；真实文件树打开、Ctrl+S 落盘、文档保留、A／B 工作区切换、跨切换 undo／redo、扩展修改及撤销、冲突拒绝覆盖、关闭取消／放弃、唯一 controller、关闭后浏览器退出 |
| 原临时宿主回归 | 原 `Test-EditorHostProbe.ps1 -Close` 43 项通过；原生焦点进出、XAML 模态框、10 次隐藏恢复、窗口缩放／最大化／最小化、自检和释放 |
| 原生 TAEF 回归 | `*WorkspacePreview*` 7 项、`*Tab*Layout*` 17 项全部通过 |
| 构建／部署隔离 | 启用与关闭 `EnableEditorHostProbe` 的包均构建通过；普通 MSIX 无 `EditorHostProbe` 资源；最新验证包已部署，前端与部署文件 SHA256 一致 |

上述检查支持继续沿用 Terminal 原生窗口与工作区，在现有文档区域使用 Monaco 的路线。它们没有证明 HWND 与所有 XAML 浮层的生产级协调，也没有覆盖 VS Code 扩展宿主、跨文件项目语言服务或退出后文档恢复。

本机不限并行数的首次构建遇到 PCH 页面文件不足；使用本文的 `/m:1 /p:CL_MPCount=2` 后通过。最终构建仍有已有的 104 项资源／打包警告，无编译错误。

本地证据保存在 `build/webview-editor-native-tests-final.log`、`build/webview-editor-native-final.png`、`build/webview-editor-scratch-regression.log`、`build/webview-editor-preview-regressions.log`、`build/webview-editor-layout-regressions.log` 和相应构建日志中。`build/webview-editor-*` 验证产物已忽略，不随源代码提交。桌面脚本可用 `-ScreenshotPath` 在关闭前保存窗口截图。

前端单测运行 `npm.cmd test`；`npm.cmd run test:browser` 用系统 Chrome／Edge 和独立临时 profile 运行真实 Monaco 及 worker，并模拟原生消息。可用 `CHROME_PATH` 指定浏览器。浏览器测试不替代桌面原生验收。

文件读写测试直接包含生产代码，可以在已设置开发环境的 PowerShell 中运行：

```powershell
cl /std:c++20 /EHsc /utf-8 /W4 /Fe:build\WorkspaceEditorBufferTests.exe /Fo:build\WorkspaceEditorBufferTests.obj tools\WorkspaceEditorBufferTests.cpp
& .\build\WorkspaceEditorBufferTests.exe
```

桌面验收使用上面启动的专用测试窗口。脚本创建全新的临时目录和样例文件，保留诊断证据；`-Close` 会关闭该测试窗口，不要在其中运行实际任务。

```powershell
$probe = Get-Process WindowsTerminal | Where-Object MainWindowTitle -eq 'WebViewEditorValidation'
$log = Get-ChildItem "$env:LOCALAPPDATA\Packages\WindowsTerminalDev_8wekyb3d8bbwe\LocalCache\Sansterminal\EditorHostProbe" -Recurse -Filter probe.log |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
$fixture = Join-Path $env:TEMP ("sansterminal-editor-ui-" + [guid]::NewGuid())
.\tools\Test-WorkspaceEditor.ps1 -ProbeProcessId $probe.Id -LogPath $log.FullName -WorkspacePath $fixture -Close
```
