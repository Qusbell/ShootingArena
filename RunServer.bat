@echo off
REM Runs a dedicated server on Lobby_Level, port 7777.
REM If it fails, edit the ENGINE_PATH line below to match your own
REM Unreal Engine install (path to UnrealEditor.exe).

set ENGINE_PATH=D:\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe

if not exist "%ENGINE_PATH%" (
    echo [ERROR] UnrealEditor.exe not found at ENGINE_PATH.
    echo Right-click this file, choose Edit, and fix the ENGINE_PATH line
    echo to point at your own engine install.
    echo Current ENGINE_PATH: %ENGINE_PATH%
    pause
    exit /b 1
)

"%ENGINE_PATH%" "%~dp0ShootingArena.uproject" Lobby_Level -server -log -Port=7777
pause
