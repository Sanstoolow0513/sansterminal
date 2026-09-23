#Requires -Version 7

<#
.SYNOPSIS
    注册本地编译出的 Windows Terminal Dev 松散布局。
.DESCRIPTION
    对 src\cascadia\CascadiaPackage\bin\<Platform>\<Configuration>\AppX\AppxManifest.xml
    执行 Add-AppxPackage -Register。需要已开启开发人员模式。
.EXAMPLE
    .\tools\Register-TerminalDev.ps1
.EXAMPLE
    .\tools\Register-TerminalDev.ps1 -Platform x64 -Configuration Release
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86', 'ARM64')]
    [string]$Platform = 'x64',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$manifest = Join-Path $root "src\cascadia\CascadiaPackage\bin\$Platform\$Configuration\AppX\AppxManifest.xml"

if (-not (Test-Path -LiteralPath $manifest)) {
    Write-Error "找不到清单：$manifest。请先生成 CascadiaPackage（$Platform $Configuration）。"
}

Write-Host "正在注册 $manifest"
Add-AppxPackage -Register -ForceUpdateFromAnyVersion -ForceApplicationShutdown -Path $manifest

Get-AppxPackage -Name 'WindowsTerminalDev*' |
    Select-Object Name, Version, PackageFullName, InstallLocation, Status |
    Format-List
