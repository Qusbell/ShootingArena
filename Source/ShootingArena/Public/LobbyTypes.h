#pragma once

#include "CoreMinimal.h"
#include "LobbyTypes.generated.h"

class APlayerState;

// 대기 로비 슬롯(좌측 칸 하나)의 상태입니다.
UENUM(BlueprintType)
enum class ELobbySlotType : uint8
{
	Open	UMETA(DisplayName = "Open"),		// 비어있고 참가 가능
	Locked	UMETA(DisplayName = "Locked"),		// 방장이 잠가서 참가 불가
	Player	UMETA(DisplayName = "Player"),		// 실제 접속한 플레이어가 차지함
	AI		UMETA(DisplayName = "AI")			// 방장이 AI로 채움
};

// 대기 로비에서 AI 슬롯에 적용하는 난이도입니다.
UENUM(BlueprintType)
enum class ELobbyDifficulty : uint8
{
	Easy	UMETA(DisplayName = "Easy"),
	Normal	UMETA(DisplayName = "Normal"),
	Hard	UMETA(DisplayName = "Hard")
};

// 대기 로비 슬롯 하나의 정보입니다. ALobbyGameState::Slots 배열의 원소로 복제됩니다.
USTRUCT(BlueprintType)
struct FLobbySlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	ELobbySlotType SlotType = ELobbySlotType::Open;

	// SlotType == Player 일 때만 유효합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	TObjectPtr<APlayerState> OwningPlayerState = nullptr;

	// Player: 접속한 플레이어의 표시 이름 / AI: 방장이 설정한 AI 이름 (예: AI_1)
	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	FString DisplayName;

	// SlotType == AI 일 때만 유효합니다.
	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	ELobbyDifficulty Difficulty = ELobbyDifficulty::Normal;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	bool bReady = false;
};
