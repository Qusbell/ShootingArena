#include "MyPlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Engine/World.h"

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
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 여러 클라이언트가 결과창에서 동시에 "로비로"를 눌러도 ServerTravel 이 한 번만
	// 실행되도록 가드합니다. ServerTravel 이 예약되면 World->NextURL 이 채워집니다.
	if (!World->NextURL.IsEmpty() || World->IsInSeamlessTravel())
	{
		return;
	}

	// bAbsolute=true 필수 — 매치 시작 때 붙였던 "?Game=BP_MultiplayerAIGameMode" 같은
	// 이전 레벨의 URL 옵션이 이어지면 Lobby_Level 이 엉뚱한 GameMode 로 열립니다.
	// (Lobby_Level 의 World Settings > GameMode Override 가 BP_LobbyGameMode 여야 함)
	World->ServerTravel(TEXT("/Game/QuakeLike_1_0/GameFlow/Lobby/Lobby_Level"), true);
}

// GetNetMode()는 블루프린트에 노출되어 있지 않아서 감싸둔 함수입니다.
bool AMyPlayerController::IsStandaloneGame() const
{
	return GetNetMode() == NM_Standalone;
}

// 결과창에서 매치 서버를 떠날 때 호출됩니다. 이 프로세스 자신을 정상 종료시킵니다.
// 에디터에서 리슨 서버로 테스트할 때 실수로 에디터 프로세스 자체가 꺼지지 않도록,
// 진짜 데디케이티드 서버 프로세스에서만 실제로 종료합니다.
void AMyPlayerController::ServerShutdownMatchServer_Implementation()
{
	if (!IsRunningDedicatedServer())
	{
		return;
	}

	FPlatformMisc::RequestExit(false);
}