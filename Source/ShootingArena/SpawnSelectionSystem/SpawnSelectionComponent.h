#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpawnSelectionComponent.generated.h"

class AController;
class APawn;
class USceneComponent;

/**
 * 260824 스폰 기획서 기준의 Player / AI 공용 스폰 컴포넌트입니다.
 *
 * 책임:
 * - 레벨에 배치된 SpawnPoint 등록
 * - 최초 스폰 분산 규칙
 * - 사망 위치 기반 리스폰 규칙
 * - Pawn Spawn / Possess
 * - SpawnPoint Rotation을 Controller와 소유 Client에 적용
 *
 * 비책임:
 * - Respawn 대기시간
 * - 사망 판정
 * - 체력/아머/인벤토리/버프 등 캐릭터 게임플레이 상태 초기화
 * - 이동 Velocity/Force 등 캐릭터 상태 초기화
 *
 * BP_QuakeGameMode에 단 하나의 인스턴스를 두고 Player와 AI가 공유해야 합니다.
 */
UCLASS(ClassGroup=(Spawn), meta=(BlueprintSpawnableComponent))
class USpawnSelectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpawnSelectionComponent();

	// ---------------------------------------------------------------------
	// Setup
	// ---------------------------------------------------------------------

	/**
	 * 레벨에 배치된 SpawnPoint SceneComponent들을 등록합니다.
	 * BP_PlayerSpawnPoint Actor 자체가 아니라 실제 SpawnPoint SceneComponent를 넘깁니다.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawn|Setup")
	void InitializeSpawnPoints(const TArray<USceneComponent*>& InSpawnPoints);

	/** 현재 등록된 유효 SpawnPoint 수를 반환합니다. */
	UFUNCTION(BlueprintPure, Category="Spawn|Setup")
	int32 GetSpawnPointCount() const;

	// ---------------------------------------------------------------------
	// Initial Spawn - Selection State
	// ---------------------------------------------------------------------

	/**
	 * 최초 스폰 배정을 시작하기 전에 1회 호출합니다.
	 * TotalCharacterCount는 최초 Player + 최초 AI 전체 수여야 합니다.
	 *
	 * 260824 PPT 규칙:
	 * - SpawnPoint >= Character: 선택된 포인트는 다음 후보에서 제외
	 * - SpawnPoint < Character:
	 *   MaxPlacement = Ceil(TotalCharacterCount / SpawnPointCount)
	 */
	UFUNCTION(BlueprintCallable, Category="Spawn|Initial")
	bool PrepareInitialSpawn(int32 TotalCharacterCount);

	/**
	 * 최초 배치 준비가 끝난 뒤 예상하지 못한 신규 참가자가 추가될 때만 사용합니다.
	 * 기존 배정 상태는 유지하면서 전체 예정 수와 최대 배치 수를 증가시킵니다.
	 *
	 * 정상적인 매치 시작에서는 PrepareInitialSpawn에 정확한 Player+AI 전체 수를 넣는 것이 우선입니다.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawn|Initial")
	bool ExtendInitialSpawn(int32 AdditionalCharacterCount);

	/**
	 * 최초 스폰 위치 하나를 선정합니다.
	 * SpawnActor를 BP에서 직접 할 경우에만 사용합니다.
	 * SpawnInitialPawn()을 사용하면 선정/실패 Rollback까지 내부에서 처리합니다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="Spawn|Initial")
	bool SelectInitialSpawnTransform(
		FTransform& OutSpawnTransform,
		int32& OutSpawnPointIndex);

	/** BP에서 직접 SpawnActor가 실패했을 때 최초 배정 횟수를 되돌립니다. */
	UFUNCTION(BlueprintCallable, Category="Spawn|Initial")
	void ReleaseInitialSpawnSelection(int32 SpawnPointIndex);

	/** 최초 스폰 준비 여부를 반환합니다. */
	UFUNCTION(BlueprintPure, Category="Spawn|Initial")
	bool IsInitialSpawnPrepared() const;

	/** 최초 스폰으로 아직 배정해야 할 캐릭터 수를 반환합니다. */
	UFUNCTION(BlueprintPure, Category="Spawn|Initial")
	int32 GetInitialAssignmentsRemaining() const;

	/** 현재 최초 스폰 세션에 등록된 전체 예정 캐릭터 수를 반환합니다. */
	UFUNCTION(BlueprintPure, Category="Spawn|Initial")
	int32 GetInitialTotalCharacterCount() const;

	// ---------------------------------------------------------------------
	// Respawn - Selection
	// ---------------------------------------------------------------------

	/**
	 * 260824 PPT 리스폰 규칙에 따라 위치 하나를 선정합니다.
	 *
	 * 1. 사망 위치와 모든 SpawnPoint 직선거리 계산
	 * 2. 먼 순서 정렬
	 * 3. CandidateCount = Max(1, Floor(SpawnPointCount / 2))
	 * 4. 가장 먼 후보 중 Random
	 *
	 * 주변 Player / AI의 존재 여부는 검사하지 않습니다.
	 * Random을 수행하므로 Blueprint Pure로 노출하지 않습니다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure=false, Category="Spawn|Respawn")
	bool SelectRespawnTransform(
		const FVector& DeathLocation,
		FTransform& OutSpawnTransform,
		int32& OutSpawnPointIndex);

	// ---------------------------------------------------------------------
	// High-level Spawn API
	// ---------------------------------------------------------------------

	/**
	 * 최초 스폰의 전체 처리 함수입니다.
	 * Select -> SpawnActor -> 실패 Rollback -> Possess -> SpawnPoint 회전 적용을 한 번에 처리합니다.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawn|Initial")
	bool SpawnInitialPawn(
		AController* Controller,
		TSubclassOf<APawn> PawnClass,
		APawn*& OutSpawnedPawn,
		FTransform& OutSpawnTransform,
		int32& OutSpawnPointIndex);

	/**
	 * 사망 후 리스폰의 전체 처리 함수입니다.
	 * SelectRespawnTransform -> SpawnActor -> Possess -> SpawnPoint 회전 적용을 한 번에 처리합니다.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawn|Respawn")
	bool RespawnPawn(
		AController* Controller,
		TSubclassOf<APawn> PawnClass,
		const FVector& DeathLocation,
		APawn*& OutSpawnedPawn,
		FTransform& OutSpawnTransform,
		int32& OutSpawnPointIndex);

	/**
	 * 기존 BP 호환용 회전 적용 함수입니다.
	 * 새 SpawnInitialPawn/RespawnPawn에서는 내부에서 자동 호출합니다.
	 *
	 * 서버 ControlRotation을 변경하고 PlayerController라면 ClientSetRotation도 호출합니다.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawn|State")
	void ApplySpawnRotation(AController* Controller, const FRotator& SpawnRotation);

private:
	/** 실제 Spawn Transform을 제공하는 SceneComponent 목록입니다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> SpawnPoints;

	/** SpawnPoints와 같은 Index를 사용하는 최초 스폰 배정 횟수입니다. */
	TArray<int32> InitialSpawnCounts;

	/** 현재 최초 스폰 세션의 전체 예정 캐릭터 수입니다. */
	int32 InitialTotalCharacterCount = 0;

	/** SpawnPoint 하나당 최초 스폰 최대 배치 수입니다. */
	int32 InitialMaxPerPoint = 0;

	/** 아직 위치를 배정하지 않은 최초 스폰 대상 수입니다. */
	int32 InitialAssignmentsRemaining = 0;

	/** PrepareInitialSpawn이 정상적으로 완료되었는지 나타냅니다. */
	bool bInitialSpawnPrepared = false;

	/** 실제 Pawn을 생성하고 Possess/SpawnPoint 회전을 적용하는 공통 내부 함수입니다. */
	bool SpawnPawnAtTransform(
		AController* Controller,
		TSubclassOf<APawn> PawnClass,
		const FTransform& SpawnTransform,
		APawn*& OutSpawnedPawn);

	/** 현재 TotalCharacterCount와 SpawnPoint 수로 최초 최대 배치 수를 다시 계산합니다. */
	bool RecalculateInitialMaxPerPoint();
};
