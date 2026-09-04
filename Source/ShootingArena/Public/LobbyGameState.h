#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "LobbyTypes.h"
#include "LobbyGameState.generated.h"

/**
 * 대기 로비(LobbyLevel)의 슬롯 목록과 현재 선택된 맵을 보관하는 GameState.
 * 모든 클라이언트에 Slots가 복제되며, UI(WBP_Lobby)는 이 값을 구독해서 표시합니다.
 */
UCLASS()
class SHOOTINGARENA_API ALobbyGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ALobbyGameState();

	UPROPERTY(ReplicatedUsing = OnRep_Slots, BlueprintReadOnly, Category = "Lobby")
	TArray<FLobbySlot> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_SelectedMapID, BlueprintReadOnly, Category = "Lobby")
	FString SelectedMapID;

	// 첫 번째로 접속한(=방장) 플레이어입니다. 방장이 나가면 다음 플레이어로 넘어갑니다.
	UPROPERTY(ReplicatedUsing = OnRep_HostPlayerState, BlueprintReadOnly, Category = "Lobby")
	TObjectPtr<APlayerState> HostPlayerState = nullptr;

	// 매치 서버(로비 서버가 게임 시작 시 별도 프로세스로 띄우는 서버)의 접속 주소입니다.
	// 비어있으면 "지금 도는 매치 없음"을 뜻합니다. 값이 채워지면 각 클라이언트는
	// (서버가 강제로 이동시키는 게 아니라) 각자 원할 때 이 주소로 직접 접속(open)합니다 —
	// 이렇게 해야 "결과창에서 로비로 돌아가기"를 각자 따로 누를 수 있습니다.
	// BlueprintReadWrite인 이유: Slots/HostPlayerState와 달리 이 값은 전용 C++ 함수 없이
	// BP_LobbyGameMode::OnLobbyStartGameApproved에서 블루프린트가 직접 채워넣습니다.
	UPROPERTY(ReplicatedUsing = OnRep_MatchServerAddress, BlueprintReadWrite, Category = "Lobby")
	FString MatchServerAddress;

	// 맵이 정해졌을 때(최초 진입 또는 방장이 맵 변경) 슬롯 배열을 새로 구성합니다. 서버 전용입니다.
	// 기존에 채워져 있던 Player/AI 슬롯은 앞쪽 인덱스부터 최대한 유지합니다.
	void RebuildSlots(const FString& NewMapID, int32 NewMaxPlayerCount);

	// 비어있는(Open) 슬롯 하나를 찾아 플레이어를 배정합니다. 서버 전용입니다.
	// 반환값: 배정된 슬롯 인덱스, 빈 슬롯이 없으면 INDEX_NONE.
	int32 AssignPlayerToOpenSlot(APlayerState* PlayerState);

	// 플레이어가 차지하고 있던 슬롯을 다시 Open으로 되돌립니다. 서버 전용입니다.
	void ReleasePlayerSlot(APlayerState* PlayerState);

	// Player가 아닌 슬롯(AI/Locked)을 전부 Open으로 되돌립니다. 서버 전용입니다.
	// 이전 매치가 끝나고 로비로 돌아왔을 때, 지난 세션에 방장이 세팅해둔 AI 슬롯이
	// 그대로 남아 다음 매치의 AI 수에 누적되는 것을 막기 위해 호출합니다.
	void ResetNonPlayerSlots();

	// 현재 Slots 의 AI 슬롯 구성을 GameInstance(SavedLobbyAISlots)에 저장합니다. 서버 전용입니다.
	// 방장이 AI 슬롯을 추가/수정/제거할 때마다 호출하면, 매치를 다녀온 뒤에도 그 구성이 유지됩니다.
	void SaveAISlotsToGameInstance() const;

	// GameInstance 에 저장된 AI 슬롯 구성을 현재 Slots 에 다시 적용합니다. 서버 전용입니다.
	// SlotIndex 가 현재 슬롯 범위 안이고, 그 슬롯을 사람이 차지하고 있지 않을 때만 적용합니다.
	// RebuildSlots 끝에서 호출되어, 로비 (재)구성 후 AI 슬롯이 복원됩니다.
	void RestoreAISlotsFromGameInstance();

	// PlayerState가 차지한 슬롯의 인덱스를 찾습니다. 없으면 INDEX_NONE.
	UFUNCTION(BlueprintPure, Category = "Lobby")
	int32 FindSlotIndexForPlayer(APlayerState* PlayerState) const;

	// 방장을 제외한 모든 Player 슬롯이 준비 완료 상태인지 확인합니다. (AI 슬롯은 항상 준비완료로 취급)
	UFUNCTION(BlueprintPure, Category = "Lobby")
	bool AreAllNonHostPlayersReady() const;

	// 현재 채워진(Player 또는 AI) 슬롯 수를 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Lobby")
	int32 GetFilledSlotCount() const;

	// 현재 Slots 배열에서 난이도별 AI 슬롯 수를 "매번 새로" 세어 돌려줍니다.
	// BP_LobbyGameMode 가 자체 멤버 변수(MatchCountEasy 등)에 ForEachLoop 로 누적하던 것을
	// 이 함수 호출로 대체하세요. 멤버 변수는 GameMode 인스턴스에 남아서 매 매치마다 값이
	// 더해지는 버그(1 -> 2 -> 3 ...)의 원인이었습니다.
	UFUNCTION(BlueprintPure, Category = "Lobby")
	void CountAISlotsByDifficulty(int32& OutEasy, int32& OutNormal, int32& OutHard) const;

	// Slots / SelectedMapID / HostPlayerState 중 하나라도 갱신되면 (서버, 클라 모두) 호출됩니다.
	// GameMode/PlayerController가 슬롯을 직접 수정한 뒤 호출하며, UI 위젯은 여기에 바인딩해서 새로고침하면 됩니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Lobby")
	void OnLobbyStateChanged();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_Slots();

	UFUNCTION()
	void OnRep_SelectedMapID();

	UFUNCTION()
	void OnRep_HostPlayerState();

	UFUNCTION()
	void OnRep_MatchServerAddress();
};
