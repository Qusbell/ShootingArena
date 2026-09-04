#include "MyPlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
// 단, "일회용으로 스폰된 매치 서버 프로세스" 일 때만 종료합니다.
// 학교 배포처럼 RunServer.bat 으로 상주시키는 통일 서버(-LocalServerReadyFile 인자 없음)나
// 에디터 리슨 서버에서는 절대 종료하지 않습니다 (누가 "메인메뉴로"를 눌러도 서버가 살아있어야 함).
void AMyPlayerController::ServerShutdownMatchServer_Implementation()
{
	if (!IsRunningDedicatedServer())
	{
		return;
	}

	// 스폰된 로컬/매치 서버는 항상 "-LocalServerReadyFile=..." 인자를 갖고 실행됩니다.
	FString UnusedReadyFile;
	const bool bIsSpawnedDisposableServer = FParse::Value(FCommandLine::Get(), TEXT("LocalServerReadyFile="), UnusedReadyFile);
	if (!bIsSpawnedDisposableServer)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MatchServer] ServerShutdownMatchServer 무시: 상주(통일) 서버는 종료하지 않습니다."));
		return;
	}

	FPlatformMisc::RequestExit(false);
}