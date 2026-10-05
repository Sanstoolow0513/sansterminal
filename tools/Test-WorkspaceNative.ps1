# Copyright (c) Sansterminal contributors. Licensed under the MIT license.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        Import-Module "$PSScriptRoot/OpenConsole.psm1"
        Set-MsBuildDevEnvironment
    }
    $output = Join-Path $repo 'build/workspace-native-tests'
    New-Item -ItemType Directory -Force $output | Out-Null
    $json = Join-Path $repo 'obj/x64/vcpkg/x64-windows-static'
    & cl.exe /nologo /std:c++20 /EHsc /utf-8 /W4 /DNOMINMAX "/Fe:$output/WorkspaceEditorBufferTests.exe" "/Fo:$output/" tools/WorkspaceEditorBufferTests.cpp src/cascadia/TerminalApp/WorkspaceEditorBuffer.cpp src/cascadia/TerminalApp/WorkspaceDocument.cpp /link cldapi.lib
    if ($LASTEXITCODE) { throw 'Workspace document tests failed to compile' }
    & "$output/WorkspaceEditorBufferTests.exe"
    if ($LASTEXITCODE) { throw 'Workspace document tests failed' }
    & cl.exe /nologo /std:c++20 /EHsc /utf-8 /W4 /MT "/I$json/include" "/Fe:$output/WorkspaceLayoutTests.exe" "/Fo:$output/" tools/WorkspaceLayoutTests.cpp src/cascadia/TerminalSettingsModel/WorkspaceLayout.cpp /link "$json/lib/jsoncpp.lib"
    if ($LASTEXITCODE) { throw 'Workspace layout tests failed to compile' }
    & "$output/WorkspaceLayoutTests.exe"
    if ($LASTEXITCODE) { throw 'Workspace layout tests failed' }
} finally {
    Pop-Location
}
