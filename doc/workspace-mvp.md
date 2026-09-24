# 工作区 MVP

此 demo 以工作区作为应用中的导航单位。每个工作区拥有自己的终端标签组和可选的文件夹；切换工作区时，其他组的终端进程继续运行。系统窗口仅负责承载界面，不参与工作区管理。

## 体验步骤

1. 打开开发版 `WindowsTerminalDev`。没有指定或恢复工作区时，会进入欢迎页，不会自动启动终端。
2. 在欢迎页选择“新建工作区”并输入名称，或选择“打开文件夹”。文件夹选择器不可用时可用“输入文件夹路径”。
3. 文件夹工作区左侧显示文件列表。点击文件夹进入，点“上一级”返回；点击文本文件可在终端旁只读预览。点“刷新”重新扫描。
4. 点击标签栏的主页图标或按 `Ctrl+Shift+N` 返回工作区页面。选择另一工作区，再切回原工作区，检查两个工作区的终端会话仍在运行。

新建文件夹工作区时，第一个终端标签从该文件夹启动。工作区页面列出当前会话和可恢复的旧工作区；点击条目即可进入。显式终端命令在未选择工作区时会进入一个“未命名工作区”。关闭当前工作区的最后一个标签后回到工作区页面。

## 构建与部署

按 [构建说明](building.md)准备环境后，在已执行 `Set-MsBuildDevEnvironment` 的 PowerShell 7 中运行：

```powershell
msbuild OpenConsole.slnx /t:"Terminal\CascadiaPackage" /p:Platform=x64 /p:Configuration=Debug /m
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\DeployAppRecipe.exe" src\cascadia\CascadiaPackage\bin\x64\Debug\CascadiaPackage.build.appxrecipe
```

## 当前范围

- 文件预览为只读，支持 UTF-8 和 UTF-16 LE 文本，最多读取前 128 KB；不提供编辑和保存。
- 文件列表每次最多显示 500 项，需要手动刷新；尚未监听文件变化。
- 工作区标签组随现有会话布局持久化设置保存。关闭布局恢复功能时，重启后的工作区也不会自动恢复。
- 操作系统窗口和现有命令行兼容层仍保留；工作区切换、文件浏览和终端标签均在当前界面完成。
