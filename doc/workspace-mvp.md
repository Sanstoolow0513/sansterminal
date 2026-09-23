# 工作区 MVP

此 demo 在一个终端窗口内管理多个工作区。每个工作区拥有自己的终端标签组；切换工作区时，其他组的终端进程继续运行。工作区菜单位于标签栏或左侧标签栏顶部。

## 体验步骤

1. 打开开发版 `WindowsTerminalDev`，点击工作区菜单。
2. 选择“新建工作区”并输入名称，或选择“将文件夹作为工作区打开”。文件夹选择器不可用时可用“输入文件夹路径”。
3. 文件夹工作区左侧显示文件列表。点击文件夹进入，点“上一级”返回；点击文本文件可在终端旁只读预览。点“刷新”重新扫描。
4. 切回“默认”工作区，再切回新工作区，检查两个工作区的终端会话仍在运行。

新建文件夹工作区时，第一个终端标签从该文件夹启动。已有工作区可以从同一菜单切换。打开旧版保存的命名窗口工作区时，其标签会恢复到当前窗口。

## 构建与部署

按 [构建说明](building.md)准备环境后，在已执行 `Set-MsBuildDevEnvironment` 的 PowerShell 7 中运行：

```powershell
msbuild OpenConsole.slnx /t:"Terminal\CascadiaPackage" /p:Platform=x64 /p:Configuration=Debug /m
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\DeployAppRecipe.exe" src\cascadia\CascadiaPackage\bin\x64\Debug\CascadiaPackage.build.appxrecipe
```

## 当前范围

- 文件预览为只读，支持 UTF-8 和 UTF-16 LE 文本，最多读取前 128 KB；不提供编辑和保存。
- 文件列表每次最多显示 500 项，需要手动刷新；尚未监听文件变化。
- 工作区标签组随现有窗口布局持久化设置保存。关闭布局恢复功能时，重启后的工作区也不会自动恢复。
- 现有 `newWindow` 命令仍可使用；工作区菜单的主要操作在当前窗口内完成。
