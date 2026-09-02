@echo off
setlocal

REM ==========================================================================
REM  ShootingArena - Development Client + Server packaging
REM
REM  The launcher-installed engine (C:\Program Files\Epic Games\UE_5.6) CANNOT
REM  build Server targets ("Server targets are not currently supported from
REM  this engine distribution."). You MUST package with the source-built engine.
REM
REM  Output layout (LocalDedicatedServerLibrary looks for WindowsServer):
REM     %ARCHIVE_DIR%\Windows\ShootingArena\Binaries\Win64\ShootingArena.exe
REM     %ARCHIVE_DIR%\WindowsServer\ShootingArena\Binaries\Win64\ShootingArenaServer.exe
REM ==========================================================================

REM --- Edit these two lines if needed --------------------------------------
set "ENGINE_ROOT=C:\Users\koreait1\Documents\GitHub\UnrealEngine"
set "ARCHIVE_DIR=C:\Users\koreait1\Desktop\ShootingArena_Package"
REM -----------------------------------------------------------------------

set "PROJECT=%~dp0ShootingArena.uproject"
set "RUNUAT=%ENGINE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"

if not exist "%RUNUAT%" (
    echo [ERROR] RunUAT.bat not found: %RUNUAT%
    echo Check ENGINE_ROOT points at your source-built engine.
    pause
    exit /b 1
)

echo.
echo === [1/2] Packaging Development Server (WindowsServer) ===
echo.
call "%RUNUAT%" BuildCookRun -project="%PROJECT%" -nop4 -utf8output -target=ShootingArenaServer -platform=Win64 -serverconfig=Development -server -noclient -build -cook -stage -pak -iostore -compressed -prereqs -archive -archivedirectory="%ARCHIVE_DIR%"
if errorlevel 1 (
    echo.
    echo [ERROR] Server packaging failed.
    pause
    exit /b 1
)

echo.
echo === [2/2] Packaging Development Client (Windows) ===
echo.
call "%RUNUAT%" BuildCookRun -project="%PROJECT%" -nop4 -utf8output -target=ShootingArena -platform=Win64 -clientconfig=Development -build -cook -stage -pak -iostore -compressed -prereqs -archive -archivedirectory="%ARCHIVE_DIR%"
if errorlevel 1 (
    echo.
    echo [ERROR] Client packaging failed.
    pause
    exit /b 1
)

echo.
echo === DONE ===
echo   Client: %ARCHIVE_DIR%\Windows\ShootingArena\Binaries\Win64\ShootingArena.exe
echo   Server: %ARCHIVE_DIR%\WindowsServer\ShootingArena\Binaries\Win64\ShootingArenaServer.exe
echo.
pause
endlocal
