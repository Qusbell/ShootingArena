#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "TimerManager.h"
#include "LoadingCoordinator.generated.h"

/** 서버가 판정하고 모든 클라이언트에 복제하는 맵 전환 준비 단계입니다. */
UENUM(BlueprintType)
enum class ELoadingCoordinatorPhase : uint8
{
	Initializing UMETA(DisplayName = "게임 준비 중"),
	WaitingForPlayers UMETA(DisplayName = "참가자 준비 대기"),
	ReadyToPlay UMETA(DisplayName = "준비 완료"),
	Failed UMETA(DisplayName = "실패")
};

/**
 * 맵별로 서버가 자동 생성하는 로딩 장벽입니다.
 *
 * ExpectedHumanPlayers URL 옵션이 있으면 그 수만큼의 실제 PlayerController가
 * PostLogin될 때까지 기다립니다. AI PlayerState는 세지 않습니다. 옵션이 없는
 * 기존 맵은 현재 접속 인원을 안전한 기본값으로 사용하므로 기존 흐름을 깨지 않습니다.
 */
UCLASS(BlueprintType)
class SHOOTINGARENA_API ALoadingCoordinator : public AInfo
{
	GENERATED_BODY()

public:
	ALoadingCoordinator();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** BP_MultiplayerAIGameMode의 모든 초기화 체인 마지막에서 한 번 호출합니다. */
	UFUNCTION(BlueprintCallable, Category = "Loading")
	void MarkServerWorldReady();

	UFUNCTION(BlueprintPure, Category = "Loading")
	ELoadingCoordinatorPhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "Loading")
	int32 GetExpectedHumanPlayers() const { return ExpectedHumanPlayers; }

	UFUNCTION(BlueprintPure, Category = "Loading")
	int32 GetConnectedHumanPlayers() const { return ConnectedHumanPlayers; }

private:
	void RefreshServerState();
	int32 CountConnectedHumanPlayers() const;
	void ApplyServerReadyFallback();

	UFUNCTION()
	void OnRep_Phase();

	UPROPERTY(ReplicatedUsing = OnRep_Phase, VisibleAnywhere, BlueprintReadOnly, Category = "Loading", meta = (AllowPrivateAccess = "true"))
	ELoadingCoordinatorPhase Phase = ELoadingCoordinatorPhase::Initializing;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Loading", meta = (AllowPrivateAccess = "true"))
	int32 ExpectedHumanPlayers = 1;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Loading", meta = (AllowPrivateAccess = "true"))
	int32 ConnectedHumanPlayers = 0;

	bool bServerWorldReady = false;
	bool bExpectedCountWasProvided = false;
	float RefreshAccumulator = 0.0f;
	FTimerHandle ServerReadyFallbackTimer;
};
