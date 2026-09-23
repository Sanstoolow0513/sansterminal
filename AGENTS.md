# 项目说明（面向 Agent 与开发者）

本仓库是 [Windows Terminal](https://github.com/microsoft/terminal)（microsoft/terminal）的衍生项目（fork），基于上游代码在 MIT 许可证下开发。仓库中保留 `LICENSE` 与 `NOTICE.md`，请遵守其条款，不要在对外材料中声称与 Microsoft 存在关联。

## 构建

前置条件：

- Windows 10 2004 (build 19041) 或更高版本，并开启开发者模式
- Visual Studio 2026 (18.6+)，工作负载：Desktop Development with C++、WinUI application development
- Windows 11 SDK 10.0.26100.8249 或更高版本
- PowerShell 7+
- 构建测试项目需要 .NET Framework 4.7.2 Targeting Pack

可用 `winget configure .config\configuration.winget` 自动配置环境。

PowerShell 构建：

```powershell
Import-Module .\tools\OpenConsole.psm1
Set-MsBuildDevEnvironment
Invoke-OpenConsoleBuild
```

Cmd 构建：

```shell
.\tools\razzle.cmd
bcz
```

解决方案文件为 `OpenConsole.slnx`。GUI 调试时在 Visual Studio 中选择 x64 或 x86 平台，部署 `CascadiaPackage` 项目（详见 `doc/building.md`、`doc/Debugging.md`）。

命令行注册已生成的松散布局（需要开发人员模式）。默认注册 `x64 Debug`：

```powershell
.\tools\register-terminal.cmd
```

指定平台与配置：

```powershell
.\tools\register-terminal.cmd x64 Release
```

等价于对 `src\cascadia\CascadiaPackage\bin\<Platform>\<Configuration>\AppX\AppxManifest.xml` 执行 `Add-AppxPackage -Register`。脚本为 `tools/Register-TerminalDev.ps1`，注册的是开发包 `WindowsTerminalDev`，与商店版 Windows Terminal 不是同一个应用。清单不存在时脚本会失败，需先生成对应配置的 `CascadiaPackage`。

## 测试

测试基于 TAEF。构建完成后用 `runut /name:*<测试名>*` 运行（tools/ 目录下的包装脚本）。更多见 `doc/TAEF.md`、`doc/UniversalTest.md`、`doc/WindowsTestPasses.md`。

## 编码规范

- C++：遵循 `.clang-format` 与 `doc/STYLE.md`
- 通用编辑器配置：`.editorconfig`
- XAML：`XamlStyler.json`
- 代码组织约定：`doc/ORGANIZATION.md`
- 添加设置项的流程：`doc/AddASetting.md`、`doc/cascadia/AddASetting.md`

## Git 约定

- `origin` 是本仓库（Sanstoolow0513/sansterminal），所有分支、PR、issue 都在本仓库进行
- `upstream`（microsoft/terminal）**仅供 fetch 同步上游代码**，push 已被禁用（pushurl=DISABLED）
- gh CLI 的默认仓库已固定为 origin（`remote.origin.gh-resolved=base`）
- 历史 commit message 中的 `#12345` 编号指向**上游** microsoft/terminal 的 PR/issue，不是本仓库的编号。Agent 不得据此在本仓库查询、引用或操作这些编号对应的 PR/issue

## 文档

开发相关文档保留在 `doc/`（构建、调试、风格、测试等）。上游的特性设计文档（specs）、roadmap 与用户文档（user-docs）已移除；如需查阅历史设计背景，请访问上游仓库。
