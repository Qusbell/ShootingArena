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
