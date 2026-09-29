# 工作区 MVP

此 demo 以工作区作为应用中的导航单位。每个工作区拥有自己的终端标签组和可选的文件夹；切换工作区时，其他组的终端进程继续运行。系统窗口仅负责承载界面，不参与工作区管理。

## 体验步骤

1. 打开开发版 `WindowsTerminalDev`。没有指定或恢复工作区时，会进入欢迎页，不会自动启动终端。
2. 在欢迎页选择“新建工作区”并输入名称，或选择“打开文件夹”。文件夹选择器不可用时可用“输入文件夹路径”。
3. 文件夹工作区左侧显示可展开的文件树。点击文件夹名称展开；点击文件打开临时预览标签，双击文件固定标签。输入文件名或相对路径可在工作区中查找文件，点“刷新”重新扫描。
4. 拖动文件树或文档区域的分隔线调整宽度，使用“放大／还原”让文档占据主区域，使用“隐藏”收起文档区域。终端会话在这些操作后继续运行。
5. 点击标签栏的主页图标或按 `Ctrl+Shift+N` 返回工作区页面。选择另一工作区，再切回原工作区，检查终端会话和已打开的文档标签仍在。

新建文件夹工作区时，第一个终端标签从该文件夹启动。工作区页面列出当前会话和可恢复的旧工作区；点击条目即可进入。显式终端命令在未选择工作区时会进入一个“未命名工作区”。关闭当前工作区的最后一个标签后回到工作区页面。

## 构建与部署

按 [构建说明](building.md)准备环境后，在已执行 `Set-MsBuildDevEnvironment` 的 PowerShell 7 中运行：

```powershell
msbuild OpenConsole.slnx /t:"Terminal\CascadiaPackage" /p:Platform=x64 /p:Configuration=Debug /m
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\DeployAppRecipe.exe" src\cascadia\CascadiaPackage\bin\x64\Debug\CascadiaPackage.build.appxrecipe
```

## 当前范围

- 文档区域为只读 `RichEditBox`，支持 UTF-8 和 UTF-16 LE 文本，最多读取前 512 KB。C 系语言、Python、PowerShell、JSON、Markdown 和 XML 等文件可以看到基础词法着色；前 128 K 字符或 6000 个着色片段之后以普通文本显示。底部显示当前行，但目前没有行号边栏、编辑、保存或语言服务器提供的语义功能。
- 文件树按需展开，每个目录最多显示 500 项。底部数字是当前目录**已显示的项目数**，末尾的 `+` 表示还有项目未显示，不代表工作区总文件数。搜索最多扫描 20000 项并显示前 100 个匹配文件，跳过 `.git`、`node_modules`、`packages`、`bin`、`obj` 和 `build` 目录。
- 文件变化需要手动刷新；尚未监听文件系统。搜索在后台进行，目录展开和文档读取仍在界面线程上执行；语法着色分批进行，避免大文件着色时长时间占用界面。大型目录和大文件仍需要继续做性能验证。
- 文档标签、文件树宽度和文档宽度目前只在本次运行中随工作区切换保存；重启后不会恢复。终端标签组仍随现有会话布局持久化设置保存；关闭布局恢复功能时，重启后的工作区也不会自动恢复。
- 操作系统窗口和现有命令行兼容层仍保留；工作区切换、文件浏览和终端标签均在当前界面完成。

## 技术探索

本次先使用现有 WinUI/XAML 宿主内的原生 `RichEditBox` 验证只读语法着色和可调整布局。它让代码阅读区脱离了固定宽度的纯文本 `TextBox`，但还不是完整代码编辑器。更完整的编辑、行号、折叠和增量着色可以继续验证 Scintilla 的 Win32 控件宿主方式。Monaco／CodeMirror 需要 WebView2；[微软文档](https://learn.microsoft.com/en-us/microsoft-edge/webview2/platforms/winui2-uwp)仍将 WinUI 2 控件在 XAML Islands 中的支持列为限制，所以不能直接把它当作现有页面里的普通 XAML 控件使用。
