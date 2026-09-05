#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MyPlayerState.generated.h"

/**
 * 결과창 "로비로" 투표(매치에 접속한 모든 인간 플레이어가 눌러야 실제로 로비로 이동)를 위한 PlayerState.
 * BP_QuakePlayerState 의 부모 클래스를 이 클래스로 변경해야 합니다.
 */
UCLASS()
class SHOOTINGARENA_API AMyPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	// 결과창에서 "로비로" 버튼을 눌렀는지 여부.
	UPROPERTY(ReplicatedUsing = OnRep_WantsReturnToLobby, BlueprintReadOnly, Category = "Lobby")
	bool bWantsReturnToLobby = false;

	// 사람(PlayerController 소유)인지 여부. AI 판별에 PlayerState->GetOwner()/GetPlayerController()를
	// 직접 쓰면 안 됩니다 — APlayerController는 자신의 소유 클라이언트에게만 리플리케이트되므로,
	// 다른 클라이언트 입장에서는 남의 PlayerController가 항상 None으로 보여 AI로 오판됩니다.
	// 이 bool은 서버가 SetOwner 시점에 계산해서 그냥 복제하므로 그런 제약이 없습니다.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
	bool bIsHumanPlayer = false;

	// 로컬(소유) 클라이언트에서 호출합니다. 서버가 값을 반영한 뒤 전원 투표 여부를 확인합니다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void Server_SetWantsReturnToLobby(bool bWants);

	// 원인 불명의 프로퍼티 리플리케이션 지연/누락 문제를 피하기 위해, 서버가 투표 집계가 바뀔 때마다
	// PlayerArray의 모든 AMyPlayerState에 직접 방송(NetMulticast)해서 채워주는 캐시값입니다.
	// UI는 이 값을 읽습니다 (bWantsReturnToLobby를 매번 재계산하지 않음).
	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	int32 CachedVotedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Lobby")
	int32 CachedEligibleCount = 0;

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_UpdateReturnToLobbyVoteCounts(int32 InVotedCount, int32 InEligibleCount);

	// 결과창 위젯이 뜰 때(Construct 등) 호출하세요. 그 시점의 최신 투표 집계를 모두에게 다시 방송시켜서,
	// 이 클라이언트가 접속 타이밍 때문에 이전 방송을 놓쳤더라도 스스로 복구할 수 있게 합니다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void Server_RequestReturnToLobbyVotesRefresh();

	// 결과창 UI의 "3 / 4" 같은 표시용. Multicast로 전달받은 캐시값을 그대로 반환합니다.
	UFUNCTION(BlueprintPure, Category = "Lobby")
	void GetReturnToLobbyVoteCounts(int32& OutVotedCount, int32& OutEligibleCount) const;

	// 위와 동일한 집계를 "3 / 4" 형태의 FText 로 바로 반환합니다 (Text 바인딩에 바로 연결 가능).
	UFUNCTION(BlueprintPure, Category = "Lobby")
	FText GetReturnToLobbyVoteCountsText() const;

	// 서버 전용: GameState->PlayerArray 를 순회해 인간 플레이어 수/투표 수를 실제로 계산합니다.
	// (클라이언트에서 호출해도 동작은 하지만, bWantsReturnToLobby 리플리케이션이 지연될 수 있어
	// UI 표시에는 위의 CachedVotedCount/CachedEligibleCount(Multicast로 전달됨)를 사용합니다.)
	static void ComputeReturnToLobbyVotes(const UWorld* World, int32& OutVotedCount, int32& OutEligibleCount);

	// bWantsReturnToLobby가 바뀔 때(리플리케이션 도착 시) UI 갱신 트리거용.
	UFUNCTION(BlueprintImplementableEvent, Category = "Lobby")
	void OnWantsReturnToLobbyChanged();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void SetOwner(AActor* NewOwner) override;

private:
	UFUNCTION()
	void OnRep_WantsReturnToLobby();
};
