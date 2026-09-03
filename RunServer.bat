@echo off
setlocal

REM ============================================================
REM  ShootingArena Dedicated Server 실행 스크립트
REM
REM  이 .bat 을 "패키지 루트"(= Windows\ 와 WindowsServer\ 폴더가
REM  같이 들어있는 폴더)에 두고 더블클릭하면 됩니다.
REM  경로는 .bat 위치 기준 상대경로입니다.
REM ============================================================

REM ---- 설정 (필요시 수정) ----------------------------------
set "SERVER_EXE=WindowsServer\ShootingArena\Binaries\Win64\ShootingArenaServer.exe"
set "MAP=/Game/QuakeLike_1_0/GameFlow/Lobby/Lobby_Level"
set "GAMEMODE=/Game/QuakeLike_1_0/GameFlow/Lobby/BP_LobbyGameMode.BP_LobbyGameMode_C"
set "PORT=7777"
REM ---------------------------------------------------------

cd /d "%~dp0"

if not exist "%SERVER_EXE%" (
    echo [오류] 서버 실행 파일을 찾을 수 없습니다:
    echo        %CD%\%SERVER_EXE%
    echo        이 .bat 은 Windows\ , WindowsServer\ 폴더와 같은 위치에 있어야 합니다.
    echo.
    pause
    exit /b 1
)

echo ============================================================
echo  Dedicated Server 시작
echo   EXE  : %SERVER_EXE%
echo   MAP  : %MAP%
echo   MODE : %GAMEMODE%
echo   PORT : %PORT%
echo ============================================================
echo.

"%SERVER_EXE%" "%MAP%?Game=%GAMEMODE%" -server -log -port=%PORT%

echo.
echo 서버 프로세스가 종료되었습니다.
pause
