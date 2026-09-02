#include "LobbyPlayerController.h"
#include "LobbyGameState.h"
#include "LobbyGameMode.h"
#include "ShootingArenaGameInstance.h"
#include "LocalDedicatedServerLibrary.h"
#include "GameFramework/PlayerState.h"

ALobbyGameState* ALobbyPlayerController::GetLobbyGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<ALobbyGameState>() : nullptr;
}

bool ALobbyPlayerController::IsHost() const
{
	const ALobbyGameState* LobbyGameState = GetLobbyGameState();
	return LobbyGameState && LobbyGameState->HostPlayerState == PlayerState;
}

void ALobbyPlayerController::Server_SetSlotType_Implementation(int32 SlotIndex, ELobbySlotType NewType)
{
	if (!IsHost())
	{
		return;
	}

	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState || !LobbyGameState->Slots.IsValidIndex(SlotIndex))
	{
		return;
	}

	FLobbySlot& Slot = LobbyGameState->Slots[SlotIndex];

	// 이미 접속해있는 플레이어 슬롯은 이 함수로 건드리지 않습니다 (강퇴 기능이 아닙니다).
	if (Slot.SlotType == ELobbySlotType::Player || NewType == ELobbySlotType::Player)
	{
		return;
	}

	Slot.SlotType = NewType;
	Slot.OwningPlayerState = nullptr;
	Slot.bReady = false;

	if (NewType != ELobbySlotType::AI)
	{
		Slot.DisplayName.Empty();
	}

	LobbyGameState->OnLobbyStateChanged();
}

void ALobbyPlayerController::Server_SetAIConfig_Implementation(int32 SlotIndex, const FString& AIName, ELobbyDifficulty Difficulty)
{
	if (!IsHost())
	{
		return;
	}

	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState || !LobbyGameState->Slots.IsValidIndex(SlotIndex))
	{
		return;
	}

	FLobbySlot& Slot = LobbyGameState->Slots[SlotIndex];

	// Player가 이미 차지한 슬롯은 AI로 덮어쓰지 않습니다.
	if (Slot.SlotType == ELobbySlotType::Player)
	{
		return;
	}

	Slot.SlotType = ELobbySlotType::AI;
	Slot.OwningPlayerState = nullptr;
	Slot.DisplayName = AIName;
	Slot.Difficulty = Difficulty;
	Slot.bReady = true; // 기획서: AI는 이름과 난이도 설정 시 자동으로 준비 완료

	LobbyGameState->OnLobbyStateChanged();
}

void ALobbyPlayerController::Server_SetReady_Implementation(bool bReady)
{
	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState || !PlayerState)
	{
		return;
	}

	const int32 SlotIndex = LobbyGameState->FindSlotIndexForPlayer(PlayerState);
	if (!LobbyGameState->Slots.IsValidIndex(SlotIndex))
	{
		return;
	}

	LobbyGameState->Slots[SlotIndex].bReady = bReady;
	LobbyGameState->OnLobbyStateChanged();
}

void ALobbyPlayerController::Server_SetPlayerName_Implementation(const FString& NewName)
{
	if (!PlayerState)
	{
		return;
	}

	// 앞뒤 공백 제거 + 너무 긴 이름 방지.
	const FString TrimmedName = NewName.TrimStartAndEnd().Left(20);
	if (TrimmedName.IsEmpty())
	{
		return;
	}

	PlayerState->SetPlayerName(TrimmedName);

	// 다음 레벨(실제 게임)로 넘어간 뒤에도 이 이름을 계속 쓸 수 있도록, 접속 주소를 키로 GameInstance에 저장해둡니다.
	// (AMyReconnectionGameMode::InitNewPlayer가 새 레벨에서 이 값을 다시 읽어 PlayerState에 적용합니다.)
	if (UShootingArenaGameInstance* SAGameInstance = Cast<UShootingArenaGameInstance>(GetWorld() ? GetWorld()->GetGameInstance() : nullptr))
	{
		SAGameInstance->SetSavedNickname(PlayerState->SavedNetworkAddress, TrimmedName);
	}

	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState)
	{
		return;
	}

	const int32 SlotIndex = LobbyGameState->FindSlotIndexForPlayer(PlayerState);
	if (LobbyGameState->Slots.IsValidIndex(SlotIndex))
	{
		LobbyGameState->Slots[SlotIndex].DisplayName = TrimmedName;
		LobbyGameState->OnLobbyStateChanged();
	}
}

void ALobbyPlayerController::Server_ChangeMap_Implementation(const FString& NewMapID, int32 NewMaxPlayerCount)
{
	if (!IsHost())
	{
		return;
	}

	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState)
	{
		return;
	}

	// 기획서: 현재 인원이 새 맵의 최대 인원보다 많으면 맵 변경을 거부합니다.
	if (LobbyGameState->GetFilledSlotCount() > NewMaxPlayerCount)
	{
		return;
	}

	LobbyGameState->RebuildSlots(NewMapID, NewMaxPlayerCount);
}

void ALobbyPlayerController::Server_StartGame_Implementation()
{
	if (!IsHost())
	{
		return;
	}

	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState || !LobbyGameState->AreAllNonHostPlayersReady())
	{
		return;
	}

	if (ALobbyGameMode* LobbyGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ALobbyGameMode>() : nullptr)
	{
		LobbyGameMode->OnLobbyStartGameApproved(LobbyGameState->SelectedMapID);
	}
}

void ALobbyPlayerController::Server_CleanupMatchServer_Implementation()
{
	if (!IsHost())
	{
		return;
	}

	ULocalDedicatedServerLibrary::StopMatchServer();
}
