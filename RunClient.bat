@echo off
REM Runs a client, starting at MainMenu_Level.
REM If it fails, edit the ENGINE_PATH line below to match your own
REM Unreal Engine install (path to UnrealEditor.exe).
REM
REM To connect to a server on another PC, WBP_MainMenu's Btn_Multiplay
REM Target IP must be set to that server PC's IP (it currently defaults
REM to 127.0.0.1, which only works if the server is on this same PC).

set ENGINE_PATH=D:\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe

if not exist "%ENGINE_PATH%" (
    echo [ERROR] UnrealEditor.exe not found at ENGINE_PATH.
    echo Right-click this file, choose Edit, and fix the ENGINE_PATH line
    echo to point at your own engine install.
    echo Current ENGINE_PATH: %ENGINE_PATH%
    pause
    exit /b 1
)

"%ENGINE_PATH%" "%~dp0ShootingArena.uproject" MainMenu_Level -game -log
pause
