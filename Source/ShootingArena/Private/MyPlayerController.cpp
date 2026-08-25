#include "MyPlayerController.h"

void AMyPlayerController::PawnLeavingGame()
{
	// 원본의 destroy를 제거함.
}



// 서버에서 진짜로 실행되는 부분.
// bShouldPause가 true면 게임을 멈추고, false면 다시 풀어줌.
void AMyPlayerController::ServerSetGamePaused_Implementation(bool bShouldPause)
{
	SetPause(bShouldPause);
}