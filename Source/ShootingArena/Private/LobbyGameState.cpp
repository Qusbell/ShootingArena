#include "LobbyGameState.h"
#include "ShootingArenaGameInstance.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"

ALobbyGameState::ALobbyGameState()
{
}

void ALobbyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ALobbyGameState, Slots);
	DOREPLIFETIME(ALobbyGameState, SelectedMapID);
	DOREPLIFETIME(ALobbyGameState, HostPlayerState);
	DOREPLIFETIME(ALobbyGameState, MatchServerAddress);
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
		// 실제로 접속 중인 플레이어 슬롯만 그대로 유지합니다. AI/잠금 설정은 맵이 바뀌거나
		// (매치 종료 후 로비 복귀처럼) 로비가 다시 초기화될 때 매번 새로 설정해야 하는 값이라,
		// 여기서 그대로 들고 오면 예전 세션의 유령 AI/잠금 상태가 남아있는 버그가 됩니다.
		if (OldSlots.IsValidIndex(i) && OldSlots[i].SlotType == ELobbySlotType::Player && OldSlots[i].OwningPlayerState)
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
	// PlayerArray에는 AI 봇의 PlayerState도 섞여 있을 수 있어서(랭킹 표시용으로 AI도 PlayerState를 가짐,
	// Seamless Travel로 매치 종료 후 로비까지 그대로 넘어옴), 실제 사람이 조종하는 PlayerController가
	// 있는 경우에만 재배정합니다. AI는 AIController가 소유하므로 자동으로 걸러집니다.
	TArray<APlayerState*> RealPlayers;
	for (APlayerState* ExistingPlayer : PlayerArray)
	{
		if (ExistingPlayer && ExistingPlayer->GetPlayerController() && FindSlotIndexForPlayer(ExistingPlayer) == INDEX_NONE)
		{
			AssignPlayerToOpenSlot(ExistingPlayer);
		}

		if (ExistingPlayer && ExistingPlayer->GetPlayerController())
		{
			RealPlayers.Add(ExistingPlayer);
		}
	}

	if (HostPlayerState == nullptr && RealPlayers.Num() > 0)
	{
		HostPlayerState = RealPlayers[0];
	}

	// [DEBUG] 임시 로그 - 문제 해결되면 제거 예정
	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] RebuildSlots: NewMapID=%s NewMax=%d OldSlots.Num=%d PlayerArray.Num=%d"), *NewMapID, NewMaxPlayerCount, OldSlots.Num(), PlayerArray.Num());
	for (const FLobbySlot& Slot : Slots)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug]   -> Slot[%d] Type=%d OwningPlayerState=%s DisplayName=%s"),
			Slot.SlotIndex, (int32)Slot.SlotType, Slot.OwningPlayerState ? *Slot.OwningPlayerState->GetPlayerName() : TEXT("None"), *Slot.DisplayName);
	}

	// 매치를 다녀온 뒤(또는 로비에서 맵을 바꾼 뒤)에도 방장이 설정해둔 AI 슬롯이 유지되도록,
	// GameInstance 에 저장해둔 AI 슬롯 구성을 새로 만든 Slots 위에 다시 적용합니다.
	RestoreAISlotsFromGameInstance();

	OnLobbyStateChanged();
}

int32 ALobbyGameState::AssignPlayerToOpenSlot(APlayerState* PlayerState)
{
	if (!HasAuthority() || !PlayerState)
	{
		return INDEX_NONE;
	}

	// [DEBUG] 임시 로그 - 문제 해결되면 제거 예정
	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] AssignPlayerToOpenSlot called for %s, Slots.Num=%d"), *PlayerState->GetPlayerName(), Slots.Num());

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
	// [DEBUG] 임시 로그 - 문제 해결되면 제거 예정
	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] ReleasePlayerSlot called. HasAuthority=%d PlayerState=%s Slots.Num=%d"),
		HasAuthority(), PlayerState ? *PlayerState->GetPlayerName() : TEXT("NULL"), Slots.Num());

	if (!HasAuthority() || !PlayerState)
	{
		return;
	}

	for (FLobbySlot& Slot : Slots)
	{
		// [DEBUG] 임시 로그 - 문제 해결되면 제거 예정
		UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug]   -> checking Slot[%d] Type=%d OwningPlayerState=%s (looking for %s)"),
			Slot.SlotIndex, (int32)Slot.SlotType,
			Slot.OwningPlayerState ? *Slot.OwningPlayerState->GetPlayerName() : TEXT("NULL"),
			*PlayerState->GetPlayerName());

		if (Slot.SlotType == ELobbySlotType::Player && Slot.OwningPlayerState == PlayerState)
		{
			Slot.SlotType = ELobbySlotType::Open;
			Slot.OwningPlayerState = nullptr;
			Slot.DisplayName.Empty();
			Slot.bReady = false;

			// [DEBUG] 임시 로그 - 문제 해결되면 제거 예정
			UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug]   -> MATCH! Slot[%d] released."), Slot.SlotIndex);

			OnLobbyStateChanged();
			return;
		}
	}

	// [DEBUG] 임시 로그 - 문제 해결되면 제거 예정
	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug]   -> NO MATCHING SLOT FOUND for %s"), *PlayerState->GetPlayerName());
}

void ALobbyGameState::ResetNonPlayerSlots()
{
	if (!HasAuthority())
	{
		return;
	}

	bool bChanged = false;

	for (FLobbySlot& Slot : Slots)
	{
		if (Slot.SlotType == ELobbySlotType::Player)
		{
			continue;
		}

		// 이미 깨끗한 Open 슬롯이면 건너뜁니다.
		if (Slot.SlotType == ELobbySlotType::Open
			&& Slot.DisplayName.IsEmpty()
			&& !Slot.bReady)
		{
			continue;
		}

		Slot.SlotType = ELobbySlotType::Open;
		Slot.OwningPlayerState = nullptr;
		Slot.DisplayName.Empty();
		Slot.Difficulty = ELobbyDifficulty::Normal;
		Slot.bReady = false;
		bChanged = true;
	}

	// AI 슬롯을 실제로 비웠으므로, 유지용 저장본도 비워서 다음 RebuildSlots 때 되살아나지 않게 합니다.
	SaveAISlotsToGameInstance();

	if (bChanged)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] ResetNonPlayerSlots: AI/Locked 슬롯을 Open으로 초기화했습니다."));
		OnLobbyStateChanged();
	}
}

void ALobbyGameState::SaveAISlotsToGameInstance() const
{
	if (!HasAuthority())
	{
		return;
	}

	UShootingArenaGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance<UShootingArenaGameInstance>() : nullptr;
	if (!GameInstance)
	{
		return;
	}

	GameInstance->SavedLobbyAISlots.Reset();
	for (const FLobbySlot& Slot : Slots)
	{
		if (Slot.SlotType != ELobbySlotType::AI)
		{
			continue;
		}

		FLobbySavedAISlot Saved;
		Saved.SlotIndex = Slot.SlotIndex;
		Saved.AIName = Slot.DisplayName;
		Saved.Difficulty = Slot.Difficulty;
		GameInstance->SavedLobbyAISlots.Add(Saved);
	}

	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] SaveAISlotsToGameInstance: %d개 AI 슬롯 저장."), GameInstance->SavedLobbyAISlots.Num());
}

void ALobbyGameState::RestoreAISlotsFromGameInstance()
{
	if (!HasAuthority())
	{
		return;
	}

	const UShootingArenaGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance<UShootingArenaGameInstance>() : nullptr;
	if (!GameInstance || GameInstance->SavedLobbyAISlots.Num() == 0)
	{
		return;
	}

	bool bChanged = false;
	for (const FLobbySavedAISlot& Saved : GameInstance->SavedLobbyAISlots)
	{
		if (!Slots.IsValidIndex(Saved.SlotIndex))
		{
			continue;
		}

		FLobbySlot& Slot = Slots[Saved.SlotIndex];
		if (Slot.SlotType == ELobbySlotType::Player)
		{
			// 사람이 이미 차지한 슬롯은 덮어쓰지 않습니다.
			continue;
		}

		Slot.SlotType = ELobbySlotType::AI;
		Slot.OwningPlayerState = nullptr;
		Slot.DisplayName = Saved.AIName;
		Slot.Difficulty = Saved.Difficulty;
		Slot.bReady = true; // 기획서: AI 슬롯은 자동 준비 완료
		bChanged = true;
	}

	if (bChanged)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] RestoreAISlotsFromGameInstance: AI 슬롯 복원 완료."));
		OnLobbyStateChanged();
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

void ALobbyGameState::CountAISlotsByDifficulty(int32& OutEasy, int32& OutNormal, int32& OutHard) const
{
	OutEasy = 0;
	OutNormal = 0;
	OutHard = 0;

	for (const FLobbySlot& Slot : Slots)
	{
		if (Slot.SlotType != ELobbySlotType::AI)
		{
			continue;
		}

		switch (Slot.Difficulty)
		{
		case ELobbyDifficulty::Easy:   ++OutEasy;   break;
		case ELobbyDifficulty::Normal: ++OutNormal; break;
		case ELobbyDifficulty::Hard:   ++OutHard;   break;
		default: break;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] CountAISlotsByDifficulty: Easy=%d Normal=%d Hard=%d (Slots.Num=%d)"),
		OutEasy, OutNormal, OutHard, Slots.Num());
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

void ALobbyGameState::OnRep_MatchServerAddress()
{
	OnLobbyStateChanged();
}
