@echo off
setlocal
cd /d "%~dp0"
title OuterWilds Debug Launcher

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run.ps1" -Configuration Debug -Diagnostics -ContactDebug -SkipWelcome
if errorlevel 1 (
    echo.
    echo Launch failed. Review the message above.
    pause
)
