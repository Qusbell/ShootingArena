@echo off
setlocal

REM ============================================================================
REM  ShootingArena - Development Client + Development Server 패키징 스크립트
REM
REM  주의: 런처로 설치한 엔진(C:\Program Files\Epic Games\UE_5.6)은
REM        Server 타겟을 빌드/패키징할 수 없습니다
REM        ("Server targets are not currently supported from this engine distribution.").
REM        따라서 반드시 소스 빌드 엔진으로 패키징해야 합니다.
REM
REM  결과 레이아웃 (LocalDedicatedServerLibrary 가 WindowsServer 를 찾습니다):
REM     %ARCHIVE_DIR%\Windows\ShootingArena\Binaries\Win64\ShootingArena.exe
REM     %ARCHIVE_DIR%\WindowsServer\ShootingArena\Binaries\Win64\ShootingArenaServer.exe
REM ============================================================================

REM --- 필요 시 이 두 줄만 수정하세요 ---------------------------------------------
set ENGINE_ROOT=C:\Users\koreait1\Documents\GitHub\UnrealEngine
set ARCHIVE_DIR=C:\Users\koreait1\Desktop\ShootingArena_Package
REM ---------------------------------------------------------------------------

set PROJECT=%~dp0ShootingArena.uproject
set RUNUAT=%ENGINE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat

if not exist "%RUNUAT%" (
    echo [ERROR] RunUAT.bat 을 찾을 수 없습니다: %RUNUAT%
    echo 소스 빌드 엔진 경로가 맞는지 ENGINE_ROOT 를 확인하세요.
    pause
    exit /b 1
)

echo.
echo === [1/2] Development Server 패키징 (WindowsServer) ===
echo.
call "%RUNUAT%" BuildCookRun ^
    -project="%PROJECT%" ^
    -nop4 -utf8output ^
    -target=ShootingArenaServer -platform=Win64 -serverconfig=Development ^
    -server -noclient ^
    -build -cook -stage -pak -iostore -compressed -prereqs ^
    -archive -archivedirectory="%ARCHIVE_DIR%"
if errorlevel 1 (
    echo.
    echo [ERROR] 서버 패키징 실패.
    pause
    exit /b 1
)

echo.
echo === [2/2] Development Client 패키징 (Windows) ===
echo.
call "%RUNUAT%" BuildCookRun ^
    -project="%PROJECT%" ^
    -nop4 -utf8output ^
    -target=ShootingArena -platform=Win64 -clientconfig=Development ^
    -build -cook -stage -pak -iostore -compressed -prereqs ^
    -archive -archivedirectory="%ARCHIVE_DIR%"
if errorlevel 1 (
    echo.
    echo [ERROR] 클라이언트 패키징 실패.
    pause
    exit /b 1
)

echo.
echo === 완료 ===
echo   클라이언트: %ARCHIVE_DIR%\Windows\ShootingArena\Binaries\Win64\ShootingArena.exe
echo   서버      : %ARCHIVE_DIR%\WindowsServer\ShootingArena\Binaries\Win64\ShootingArenaServer.exe
echo.
pause
endlocal
