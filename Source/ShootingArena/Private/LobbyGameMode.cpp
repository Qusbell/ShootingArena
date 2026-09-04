#include "LobbyGameMode.h"
#include "LobbyGameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

ALobbyGameState* ALobbyGameMode::GetLobbyGameState() const
{
	return GetGameState<ALobbyGameState>();
}

void ALobbyGameMode::InitializeLobby(const FString& DefaultMapID, int32 MaxPlayerCount)
{
	if (ALobbyGameState* LobbyGameState = GetLobbyGameState())
	{
		LobbyGameState->RebuildSlots(DefaultMapID, MaxPlayerCount);
	}
}

void ALobbyGameMode::MarkMatchLaunched()
{
	bMatchLaunchedSinceLobbyReset = true;

	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] MarkMatchLaunched: 다음 로비 복귀 시 AI 슬롯을 초기화합니다."));
}

void ALobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState || !NewPlayer || !NewPlayer->PlayerState)
	{
		return;
	}

	// ★ 그 무엇보다 먼저: 이전 매치의 포트(MatchServerAddress)가 남아 있으면 즉시 비웁니다.
	// 매치를 끝내고 "open"으로 로비에 재접속한 클라는 새 연결이라 GameState를 처음부터 다시 받는데,
	// 이 값이 이전 매치 그대로 "7778" 등으로 남아 있으면 클라의 OnLobbyStateChanged가 그걸 보고
	// 이미 죽은 매치 서버로 다시 open → 접속 실패 → 메인 메뉴로 튕깁니다.
	// 슬롯 배정/OnLobbyStateChanged 등 그 어떤 것보다도 먼저 비워야 이 클라의 초기 리플리케이션에 안 실립니다.
	if (!LobbyGameState->MatchServerAddress.IsEmpty())
	{
		LobbyGameState->MatchServerAddress.Empty();
		LobbyGameState->ResetNonPlayerSlots(); // AI 슬롯 초기화 보조 경로 유지
	}

	// 매치가 한 번 나간 뒤 처음 (재)접속한 플레이어라면, 지난 세션에 방장이 세팅해둔
	// AI 슬롯을 먼저 비웁니다. (로비 서버는 세션 내내 살아있어서 Slots 배열이 유지되므로,
	// 여기서 안 비우면 다음 매치의 AI 수에 계속 누적됩니다.)
	if (bMatchLaunchedSinceLobbyReset)
	{
		bMatchLaunchedSinceLobbyReset = false;
		LobbyGameState->ResetNonPlayerSlots();
	}

	LobbyGameState->AssignPlayerToOpenSlot(NewPlayer->PlayerState);

	// 아직 방장이 없다면 (=서버에 처음 들어온 플레이어라면) 방장으로 지정합니다.
	if (LobbyGameState->HostPlayerState == nullptr)
	{
		LobbyGameState->HostPlayerState = NewPlayer->PlayerState;
		LobbyGameState->OnLobbyStateChanged();
	}
}

void ALobbyGameMode::Logout(AController* Exiting)
{
	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	APlayerState* ExitingPlayerState = Exiting ? Exiting->PlayerState : nullptr;

	// [DEBUG] 임시 로그 - 문제 해결되면 제거 예정
	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] Logout called. Exiting=%s LobbyGameState=%s ExitingPlayerState=%s"),
		Exiting ? *Exiting->GetName() : TEXT("NULL"),
		LobbyGameState ? TEXT("valid") : TEXT("NULL"),
		ExitingPlayerState ? *ExitingPlayerState->GetPlayerName() : TEXT("NULL"));

	if (LobbyGameState && ExitingPlayerState)
	{
		LobbyGameState->ReleasePlayerSlot(ExitingPlayerState);

		if (LobbyGameState->HostPlayerState == ExitingPlayerState)
		{
			// 남아있는 플레이어 중 가장 앞 슬롯을 차지한 사람에게 방장을 넘깁니다.
			APlayerState* NextHost = nullptr;
			for (const FLobbySlot& Slot : LobbyGameState->Slots)
			{
				if (Slot.SlotType == ELobbySlotType::Player && Slot.OwningPlayerState)
				{
					NextHost = Slot.OwningPlayerState;
					break;
				}
			}

			LobbyGameState->HostPlayerState = NextHost;
			LobbyGameState->OnLobbyStateChanged();
		}
	}

	Super::Logout(Exiting);
}
