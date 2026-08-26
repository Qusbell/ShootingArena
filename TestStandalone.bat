@echo off
REM 에디터와 완전히 별개의 독립 클라이언트를 실행합니다.
REM 에디터 Play 버튼(Standalone Game)과는 다릅니다 - 절대 그걸로 테스트하지 마세요.
"D:\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe" "D:\Github\ShootingArena\ShootingArena.uproject" /Game/QuakeLike_1_0/GameFlow/MainLevel/MainMenu_Level -game
