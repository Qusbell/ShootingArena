#include "LobbyGameState.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerState.h"

ALobbyGameState::ALobbyGameState()
{
}

void ALobbyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ALobbyGameState, Slots);
	DOREPLIFETIME(ALobbyGameState, SelectedMapID);
	DOREPLIFETIME(ALobbyGameState, HostPlayerState);
}

void ALobbyGameState::RebuildSlots(const FString& NewMapID, int32 NewMaxPlayerCount)
{
	if (!HasAuthority())
	{
		return;
	}

	TArray<FLobbySlot> OldSlots = Slots;

	Slots.Empty();
	Slots.Reserve(NewMaxPlayerCount);
	for (int32 i = 0; i < NewMaxPlayerCount; ++i)
	{
		if (OldSlots.IsValidIndex(i))
		{
			FLobbySlot CarriedOver = OldSlots[i];
			CarriedOver.SlotIndex = i;
			Slots.Add(CarriedOver);
		}
		else
		{
			FLobbySlot NewSlot;
			NewSlot.SlotIndex = i;
			Slots.Add(NewSlot);
		}
	}

	SelectedMapID = NewMapID;

	// 이미 접속해있는데(PostLogin이 이 함수보다 먼저 실행됨 등) 슬롯을 못 받은 플레이어를 다시 배정합니다.
	// (예: 리슨 서버 로컬 플레이어의 PostLogin이 InitializeLobby보다 먼저 실행되는 경우)
	for (APlayerState* ExistingPlayer : PlayerArray)
	{
		if (ExistingPlayer && FindSlotIndexForPlayer(ExistingPlayer) == INDEX_NONE)
		{
			AssignPlayerToOpenSlot(ExistingPlayer);
		}
	}

	if (HostPlayerState == nullptr && PlayerArray.Num() > 0)
	{
		HostPlayerState = PlayerArray[0];
	}

	OnLobbyStateChanged();
}

int32 ALobbyGameState::AssignPlayerToOpenSlot(APlayerState* PlayerState)
{
	if (!HasAuthority() || !PlayerState)
	{
		return INDEX_NONE;
	}

	for (FLobbySlot& Slot : Slots)
	{
		if (Slot.SlotType == ELobbySlotType::Open)
		{
			Slot.SlotType = ELobbySlotType::Player;
			Slot.OwningPlayerState = PlayerState;
			Slot.DisplayName = PlayerState->GetPlayerName();
			Slot.bReady = false;

			OnLobbyStateChanged();
			return Slot.SlotIndex;
		}
	}

	return INDEX_NONE;
}

void ALobbyGameState::ReleasePlayerSlot(APlayerState* PlayerState)
{
	if (!HasAuthority() || !PlayerState)
	{
		return;
	}

	for (FLobbySlot& Slot : Slots)
	{
		if (Slot.SlotType == ELobbySlotType::Player && Slot.OwningPlayerState == PlayerState)
		{
			Slot.SlotType = ELobbySlotType::Open;
			Slot.OwningPlayerState = nullptr;
			Slot.DisplayName.Empty();
			Slot.bReady = false;

			OnLobbyStateChanged();
			return;
		}
	}
}

int32 ALobbyGameState::FindSlotIndexForPlayer(APlayerState* PlayerState) const
{
	if (!PlayerState)
	{
		return INDEX_NONE;
	}

	for (const FLobbySlot& Slot : Slots)
	{
		if (Slot.SlotType == ELobbySlotType::Player && Slot.OwningPlayerState == PlayerState)
		{
			return Slot.SlotIndex;
		}
	}

	return INDEX_NONE;
}

bool ALobbyGameState::AreAllNonHostPlayersReady() const
{
	for (const FLobbySlot& Slot : Slots)
	{
		if (Slot.SlotType == ELobbySlotType::Player
			&& Slot.OwningPlayerState != HostPlayerState
			&& !Slot.bReady)
		{
			return false;
		}
	}

	return true;
}

int32 ALobbyGameState::GetFilledSlotCount() const
{
	int32 Count = 0;
	for (const FLobbySlot& Slot : Slots)
	{
		if (Slot.SlotType == ELobbySlotType::Player || Slot.SlotType == ELobbySlotType::AI)
		{
			++Count;
		}
	}
	return Count;
}

void ALobbyGameState::OnRep_Slots()
{
	OnLobbyStateChanged();
}

void ALobbyGameState::OnRep_SelectedMapID()
{
	OnLobbyStateChanged();
}

void ALobbyGameState::OnRep_HostPlayerState()
{
	OnLobbyStateChanged();
}
