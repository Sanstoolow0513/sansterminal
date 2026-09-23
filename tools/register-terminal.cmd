@echo off
rem Register the loose-layout Windows Terminal Dev package.
rem Usage: register-terminal.cmd [x64^|x86^|ARM64] [Debug^|Release]
rem Defaults: x64 Debug

where pwsh >nul 2>&1
if errorlevel 1 (
    echo pwsh not found. Install PowerShell 7+.
    exit /b 1
)

pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0Register-TerminalDev.ps1" %*
exit /b %ERRORLEVEL%
