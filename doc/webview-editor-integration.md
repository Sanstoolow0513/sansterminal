# 工作区编辑器与文档保存

工作区默认使用离线 Monaco 编辑器，复用 Terminal 的文件树、文档标签、工作区、焦点和窗口生命周期。旧 `RichEditBox` 与手工预览着色路径已移除。此前的 [Monaco 宿主验证](editor-host-probe.md) 保留为独立的临时缓冲区诊断。

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
msbuild OpenConsole.slnx /t:"Terminal\CascadiaPackage" /p:Platform=x64 /p:Configuration=Debug /p:CL_MPCount=2 /m:1
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\DeployAppRecipe.exe" src\cascadia\CascadiaPackage\bin\x64\Debug\CascadiaPackage.build.appxrecipe

& "$env:LOCALAPPDATA\Microsoft\WindowsApps\wtd.exe" -w new nt --title WorkspaceEditorValidation cmd.exe /d /k echo WorkspaceEditorValidation
```

部署前关闭开发包窗口。WebView2 依赖和离线资源现在默认构建及打包，无需 `EnableEditorHostProbe` 或 `SANSTERMINAL_WORKSPACE_EDITOR`。仅 `SANSTERMINAL_EDITOR_PROBE=1` 启用临时缓冲区诊断；普通启动不运行自检、不显示验证工具。开发包与商店版 Windows Terminal 分开。

工作区标签栏可在外观设置中选择左侧并调整宽度。打开文件夹后，点击文件进入编辑区；顶栏布局按钮进入“设置 → 工作区布局”。文件树、终端和编辑器可相对另一面板移动到左侧、右侧、上方或下方，也可在预览中拖到目标边缘，支持嵌套组合。两条比例滑块调整主分隔线和内部分隔线位置，点击“保存”后持久化；页底“放弃更改”取消本次设置编辑，页内“恢复默认布局”恢复布局与显示开关。`Ctrl+S` 保存文档，`F6` 返回终端。

文件树、终端、编辑器和标签栏各有独立显示开关，窄窗口也按用户选择显示，焦点变化不自动隐藏另一面板。隐藏区域保留终端连接、文档、撤销和查看位置；所有区域隐藏时，顶栏仍可打开工作区布局设置。设置页始终使用完整内容区域。

布局通过 `workspaceLayout` JSON 对象保存，节点使用 `direction: row/column`、`ratio`、`first`、`second`；三个叶子 `files/terminal/editor` 各出现一次。四个默认显示设置为 `workspaceShowFiles/workspaceShowTerminal/workspaceShowEditor/workspaceShowTabs`。在“设置 → 快捷方式”可绑定 `toggleWorkspaceFiles`、`toggleWorkspaceTerminal`、`toggleWorkspaceEditor`、`toggleWorkspaceTabs` 和 `openWorkspaceLayout`；切换命令控制当前窗口的显示状态，不新增默认组合键。

## 集成职责

| 层 | 职责 |
| --- | --- |
| WorkspaceDocument.h / WorkspaceEditor::DocumentService | 文档缓冲、磁盘基线、编码、保存快照、文件身份和原子保存；读写由调用层调度到后台 |
| TerminalPage | 文档身份及已授权路径、标签和布局、异步服务调度、消息验证、关闭同步和未保存提示 |
| WorkspaceLayout.h / WorkspaceLayoutPage | 共用布局树、隐藏叶子折叠和分隔线几何；设置克隆上的组合、预览、比例及独立显隐 |
| TerminalWindow | 向窗口宿主提供编辑区域和消息事件 |
| AppHost / WorkspaceEditorHost | 每窗口一个原生 HWND / WebView2 controller、离线资源、来源检查、JSON 消息传输、原生焦点、弹层和释放；EditorHostProbe 派生类仅用于诊断 |
| Monaco | 每文档一个 Model、视图位置、撤销历史、语言 worker，以及内置扩展命令 |

桥接协议版本为 `1`。原生端发送 `open / activate / close / saved / error`；前端发送 `ready / changed / save`。关闭前使用 `flush / flushed / resume` 同步最新缓冲区并暂时冻结编辑，超时取消关闭。文档消息使用原生端分配的身份；网页没有按任意路径读写文件或执行 shell 命令的接口。消息来源必须完全匹配应用的虚拟 HTTPS 页面，且有类型和长度限制。资源映射仅指向随应用打包的编辑器目录。

正常切换文档、工作区或临时隐藏编辑器都复用 Model。每个打开的文档标签都保留到显式关闭，修改后显示 `*`；这避免快速点击文件树时跨进程编辑消息尚未到达便替换文档。保存失败保留缓冲区；关闭文档、工作区或窗口时先同步完整内容，再提供保存／放弃／取消。编辑器使用 Monaco 支持的 textarea 输入路径；原生宿主处理 `Ctrl+S` 并发送 `save-active`，沿用同一保存服务。编辑器进程故障可重试，恢复原生端已收到的文字和未保存状态；恢复会重置 Monaco 撤销历史。应用退出、崩溃或系统关机后的草稿恢复尚未实现。“退出所有窗口”逐个完成各窗口的确认，取消或保存失败时停止，已确认关闭的窗口不会重新打开。

## 文件与保存边界

编辑验证面向不超过 2 MiB 的严格 UTF-8 和带 BOM 的 UTF-16 LE 文本，保留 BOM 和 LF／CRLF 换行。不支持的文本、二进制、混合／CR 换行及超限文件进入只读状态，没有截断内容回写路径。编辑缓冲区通信上限为 3 Mi 个 UTF-16 代码单元，超过后暂停同步和关闭，需缩减内容；保存后的文件仍须满足 2 MiB 字节上限。

保存前重新比较原始磁盘字节和文件身份，发现外部修改或同内容替换也拒绝覆盖并保留缓冲区。写入使用同目录临时文件和原子替换，替换前再次校验；保留原文件备份以处理替换失败，恢复失败会留下恢复文件并报告路径。这不是文件系统的原子比较并交换，其他程序恰在最终比对后替换路径的竞态仍存在。没有文件监听、自动合并、另存为或跨窗口共享缓冲；同一文件在其他窗口打开时仍检查保存冲突。

文件读取、编码和保存在后台执行。保存确认对应所提交的文本与版本快照；期间继续编辑的内容仍保持未保存，下次保存使用更新后的磁盘基线。每次内容变化仍向原生端发送完整文本，增量通信留待后续优化。关闭会等待进行中的保存；编辑器仍可用时，同步超时或内容超限会取消关闭。若同步失败后宿主退出，独立的 EditorSession / DocumentSynchronization 保留内容丢失状态，关闭提供明确的放弃／取消；重新加载和重放原生快照不会清除此状态。确认框显示完整路径，以区分同名文件。

文件只读检查读取 reparse tag，禁止符号链接及其他 name-surrogate 重解析点，允许可读取的 Cloud Files 占位文件。原子保存的临时文件和恢复副本使用独立的短同目录文件名，避免合法长文件名追加后缀后超过 NTFS 限制。文件系统实现位于 WorkspaceEditorBuffer.cpp / WorkspaceDocument.cpp；布局 JSON 在 WorkspaceLayout.cpp 通过 jsoncpp 解析，设置分层直接传入 Json::Value。

生产页面使用 `workspace.html`，诊断页面使用 `index.html`。WebView2 用户目录复用 `LocalCache/Sansterminal/WorkspaceEditor`（生产）或 `LocalCache/Sansterminal/EditorHostProbe`（诊断），不再逐窗口创建 GUID 目录；多个窗口各自持有 controller 和内存中的文档。旧 GUID 目录不会自动删除。日志位于对应目录的 `probe.log`。

## 扩展性判断

本验证提供随应用打包的可信命令注册接口，通过编辑器操作修改 Model，保留撤销记录。它验证自有编辑器功能可以接入，不代表完成插件生态。

[Monaco 官方说明](https://github.com/microsoft/monaco-editor/blob/main/README.md)明确，VS Code 扩展不能直接运行在 Monaco。当前锁定版本提供补全、hover、格式化、定义、重命名、代码操作和诊断 API，可继续接入语言服务；LSP 的连接和服务端需要单独实现。兼容 VS Code 扩展还需 [extension host](https://code.visualstudio.com/api/advanced-topics/extension-host)、相应运行时和工作台 API。

当前使用 windowed WebView2，XAML 对话框打开时隐藏原生区域，关闭后恢复同一 controller；普通 popup 仍由 50 ms 轮询协调。若要完善层叠能力，可继续验证 [composition hosting](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/windowed-vs-visual-hosting)，同时承担输入转发与 DPI 协调。网页、对话框和焦点回调遵循 [WebView2 线程模型](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model)，桥接遵循其 [安全建议](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security)。

目前 Model URI 包含工作区、文档身份和文件名，没有完整源目录树；JSON、TypeScript、CSS 和 HTML worker 已打包，但跨文件 TypeScript import 解析和整个项目的语义服务尚未验证。

## 合入前修复验证（2026-10-03）

原生回归覆盖 234／255 字符文件名的两次保存及辅助文件清理、符号链接只读、保存冲突和同步失败后的宿主退出／重新连接。布局测试通过 48 种组合、1,536 个几何／显隐用例以及 Json::Value 往返和非法输入；前端 20 项测试、真实 Chromium 的 Monaco／worker 冒烟测试及两个 `*WorkspaceLayout*` TAEF 用例通过。完整 TAEF 和窗口 UI 自动化未重跑。

Cloud Files 集成测试尝试在独立临时目录注册同步根；本机转换 API 返回成功却未创建 reparse point，因此明确跳过此用例，仍需真实 OneDrive／Cloud Files 环境验证。接近 2 MiB 的转义文本经过真实 WebView2 `PostWebMessageAsJson` 的端到端验证仍待完成。本次构建未部署到开发包。

## 此前验收（2026-10-03，可组合布局与默认编辑器）

配置为 `x64 Debug`。本轮代码的开发包、TestHostApp 和 SettingsModel 测试项目已构建通过，最新版已通过 `DeployAppRecipe.exe` 部署。主程序、TerminalApp、设置模型、设置页面 DLL、资源 PRI 和新页面 XBF 均与 MSIX 内文件的 SHA256 一致。部署后再次验证了独立首页、布局设置入口及关闭设置返回首页，未改动开发包的用户配置。

| 验证 | 结果 |
| --- | --- |
| 原生构建 | CascadiaPackage、TestHostApp 与 SettingsModel 测试项目通过；仍有既有资源与打包警告，无编译错误 |
| 前端及真实浏览器（此前验证） | 19 项单测通过；真实 Monaco、JSON／TS／CSS／HTML worker、保存快照、关闭同步、模型与撤销状态通过；本轮未重复运行 |
| 生产文档服务（此前验证） | `/W4 /WX` 通过；编码、BOM、换行、文件身份冲突、失败清理、保存期间继续编辑和过时回调保护通过；本轮未重复运行 |
| 布局纯模型 | `/W4 /WX` 通过，覆盖全部 48 种三面板布局、1536 个几何／显隐组合、序列化、独立克隆及嵌套比例调整 |
| 原生 TAEF | `*TabTests*` 102/102 通过，覆盖标题栏、布局、欢迎页、导航、焦点、文档状态及独立显隐 |
| SettingsModel TAEF | 164/164 通过，包含布局对象持久化、非法布局拒绝、设置克隆、快捷方式序列化；英文资源断言使用进程级 en-US 测试上下文并恢复 |
| 实际桌面 | Test-WorkspaceEditor.ps1 158/158 通过：实际预览拖放、嵌套组合、比例保存、分隔线拖动、设置保存／放弃更改、四区域快捷键、全隐藏后的恢复、Ctrl+S、跨文件／工作区 undo／redo、磁盘冲突和关闭确认；独立收尾验证 controller 释放与浏览器退出 |
| 全新进程首页与重启 | 15/15 通过：无启动命令时首页全宽、不自动创建终端，从首页进入／关闭设置后回到首页且移除临时工作区，重启后设置预览恢复已保存的嵌套布局 |

桌面测试在独立便携副本与全新临时目录中运行，未修改开发包的用户设置或实际项目。自动化键盘按下和释放之间保留 80 ms，焦点同时核对 `Edit` 类型、键盘焦点和 HWND 归属；输入后等待原生未保存标记，再验证保存／关闭。当前证据为本地 `build/webview-editor-layout-*`，测试目录和专用配置记录在 `build/webview-editor-layout-fixture.json`，首页／重启补测记录在 `build/webview-editor-layout-home-test.log` 和 `build/webview-editor-layout-home-fixture.json`；此前默认编辑器的前端及文档服务证据保留在 `build/webview-editor-current-*`。中文输入法候选框、跨屏 DPI、系统关机恢复与长期资源占用仍未完成验收。

## 历史验证（2026-10-02，可选编辑器）

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
.\tools\Test-WorkspaceNative.ps1
```

完整桌面验收使用独立便携副本及其全新测试配置，设置 `tabPosition: left` 并使用不运行实际任务的 shell profile。专用配置的 `actions` 与 `keybindings` 需要以下绑定；这些键只属于测试配置：

| 测试键 | 命令 |
| --- | --- |
| Ctrl+Alt+F9 | `toggleWorkspaceFiles` |
| Ctrl+Alt+F10 | `toggleWorkspaceTerminal` |
| Ctrl+Alt+F11 | `toggleWorkspaceEditor` |
| Ctrl+Alt+F12 | `toggleWorkspaceTabs` |
| Ctrl+Alt+L | `openWorkspaceLayout` |
| Ctrl+Alt+Backspace | `closeTab` |

启动该副本后，将其 PID、该进程生成的 `probe.log` 和便携副本的 `settings/settings.json` 路径分别赋给 `$testProcessId`、`$testProbeLog`、`$testSettingsPath`。脚本会通过设置 UI 修改该专用配置，因此 `SettingsPath` 必须指向测试副本。脚本创建全新的临时目录和样例文件，保留诊断证据；`-Close` 关闭专用测试窗口。

```powershell
$fixture = Join-Path $env:TEMP ("sansterminal-editor-ui-" + [guid]::NewGuid())
.\tools\Test-WorkspaceEditor.ps1 -ProbeProcessId $testProcessId -LogPath $testProbeLog -SettingsPath $testSettingsPath -WorkspacePath $fixture -Close
```
