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

void ALobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState || !NewPlayer || !NewPlayer->PlayerState)
	{
		return;
	}

	LobbyGameState->AssignPlayerToOpenSlot(NewPlayer->PlayerState);

	// 매치 서버 주소를 초기화합니다. 이전 매치가 끝나고 돌아온 플레이어가 "새로" 로비에 접속하면
	// (같은 프로세스 안에서 계속 있던 게 아니라 open으로 재접속한 거라) GameState를 처음 받는 셈이라,
	// MatchServerAddress 값이 이전 매치 그대로 "7778" 등으로 남아있으면 그 초기 리플리케이션만으로도
	// OnRep_MatchServerAddress가 발동해서 방금 나온 매치 서버로 다시 끌려가버리는 버그가 있었습니다.
	// 누군가 로비에 (재)접속하는 시점엔 이미 이전 매치는 완전히 끝난 뒤이므로 안전하게 비워줍니다.
	if (!LobbyGameState->MatchServerAddress.IsEmpty())
	{
		LobbyGameState->MatchServerAddress.Empty();
	}

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
