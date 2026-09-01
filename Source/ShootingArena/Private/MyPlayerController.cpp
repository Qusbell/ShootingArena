#include "MyPlayerController.h"

void AMyPlayerController::PawnLeavingGame()
{
	// ������ destroy�� ������.
}



// �������� ��¥�� ����Ǵ� �κ�.
// bShouldPause�� true�� ������ ���߰�, false�� �ٽ� Ǯ����.
void AMyPlayerController::ServerSetGamePaused_Implementation(bool bShouldPause)
{

    int32 PlayerCount = 0;

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (IsValid(PC))
        {
            ++PlayerCount;
        }
    }

    if (PlayerCount >= 2)
    {
        // 2�� �̻��̸� ���� ��ü pause�� ���� ����
        return;
    }


	SetPause(bShouldPause);
}

// GameMode가 서버 권한으로 직접 호출하는 강제 일시정지입니다. 인원수 제한 없이 항상 적용됩니다.
void AMyPlayerController::SetGamePausedByGameMode(bool bShouldPause)
{
	SetPause(bShouldPause);
}

// 결과창의 "로비로 돌아가기" 버튼에서 호출합니다. 누가 눌러도 서버 전체가 Lobby_Level로 travel합니다.
// bAbsolute=true를 반드시 넘겨야 합니다 — 안 그러면 매치 시작 때 붙였던 "?Game=BP_MultiplayerAIGameMode"
// 같은 이전 레벨의 URL 옵션이 그대로 이어져서, Lobby_Level이 엉뚱한 GameMode(그리고 그에 딸린
// BP_QuakePlayerController)로 열려버립니다 — 그러면 BP_LobbyPlayerController가 안 쓰여서
// WBP_Lobby가 아예 생성되지 않고 화면이 검게 보이는 버그로 이어집니다.
void AMyPlayerController::ServerReturnToLobby_Implementation()
{
	if (UWorld* World = GetWorld())
	{
		World->ServerTravel(TEXT("/Game/QuakeLike_1_0/GameFlow/Lobby/Lobby_Level"), true);
	}
}

// GetNetMode()는 블루프린트에 노출되어 있지 않아서 감싸둔 함수입니다.
bool AMyPlayerController::IsStandaloneGame() const
{
	return GetNetMode() == NM_Standalone;
}