#include "SpawnSelectionComponent.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpawnSelection, Log, All);

USpawnSelectionComponent::USpawnSelectionComponent()
{
	// 스폰 시스템은 이벤트 기반으로만 동작하므로 Tick이 필요하지 않습니다.
	PrimaryComponentTick.bCanEverTick = false;
}

void USpawnSelectionComponent::InitializeSpawnPoints(
	const TArray<USceneComponent*>& InSpawnPoints)
{
	SpawnPoints.Reset();

	for (USceneComponent* SpawnPoint : InSpawnPoints)
	{
		if (IsValid(SpawnPoint))
		{
			SpawnPoints.Add(SpawnPoint);
		}
	}

	// SpawnPoint 목록이 바뀌면 기존 최초 스폰 배정 상태는 사용할 수 없습니다.
	InitialSpawnCounts.Reset();
	InitialTotalCharacterCount = 0;
	InitialMaxPerPoint = 0;
	InitialAssignmentsRemaining = 0;
	bInitialSpawnPrepared = false;

	UE_LOG(LogSpawnSelection, Log,
		TEXT("InitializeSpawnPoints: %d valid spawn points registered."),
		SpawnPoints.Num());
}

int32 USpawnSelectionComponent::GetSpawnPointCount() const
{
	int32 ValidCount = 0;

	for (const TObjectPtr<USceneComponent>& SpawnPoint : SpawnPoints)
	{
		if (IsValid(SpawnPoint))
		{
			++ValidCount;
		}
	}

	return ValidCount;
}

bool USpawnSelectionComponent::PrepareInitialSpawn(
	const int32 TotalCharacterCount)
{
	bInitialSpawnPrepared = false;
	InitialSpawnCounts.Reset();
	InitialTotalCharacterCount = 0;
	InitialMaxPerPoint = 0;
	InitialAssignmentsRemaining = 0;

	if (TotalCharacterCount <= 0)
	{
		UE_LOG(LogSpawnSelection, Warning,
			TEXT("PrepareInitialSpawn failed: TotalCharacterCount must be greater than 0."));
		return false;
	}

	if (GetSpawnPointCount() <= 0)
	{
		UE_LOG(LogSpawnSelection, Warning,
			TEXT("PrepareInitialSpawn failed: no valid SpawnPoint is registered."));
		return false;
	}

	InitialSpawnCounts.Init(0, SpawnPoints.Num());
	InitialTotalCharacterCount = TotalCharacterCount;
	InitialAssignmentsRemaining = TotalCharacterCount;

	if (!RecalculateInitialMaxPerPoint())
	{
		InitialSpawnCounts.Reset();
		InitialTotalCharacterCount = 0;
		InitialAssignmentsRemaining = 0;
		return false;
	}

	bInitialSpawnPrepared = true;

	UE_LOG(LogSpawnSelection, Log,
		TEXT("PrepareInitialSpawn: Total=%d, SpawnPoints=%d, MaxPerPoint=%d"),
		InitialTotalCharacterCount,
		GetSpawnPointCount(),
		InitialMaxPerPoint);

	return true;
}

bool USpawnSelectionComponent::ExtendInitialSpawn(
	const int32 AdditionalCharacterCount)
{
	if (AdditionalCharacterCount <= 0)
	{
		return false;
	}

	// BeginPlay 이후 첫 신규 참가자가 들어오는 경우에도 안전하게 시작할 수 있게 합니다.
	if (!bInitialSpawnPrepared)
	{
		return PrepareInitialSpawn(AdditionalCharacterCount);
	}

	InitialTotalCharacterCount += AdditionalCharacterCount;
	InitialAssignmentsRemaining += AdditionalCharacterCount;

	if (!RecalculateInitialMaxPerPoint())
	{
		InitialTotalCharacterCount -= AdditionalCharacterCount;
		InitialAssignmentsRemaining -= AdditionalCharacterCount;
		return false;
	}

	UE_LOG(LogSpawnSelection, Log,
		TEXT("ExtendInitialSpawn: Added=%d, NewTotal=%d, Remaining=%d, MaxPerPoint=%d"),
		AdditionalCharacterCount,
		InitialTotalCharacterCount,
		InitialAssignmentsRemaining,
		InitialMaxPerPoint);

	return true;
}

bool USpawnSelectionComponent::RecalculateInitialMaxPerPoint()
{
	const int32 ValidSpawnPointCount = GetSpawnPointCount();

	if (InitialTotalCharacterCount <= 0 || ValidSpawnPointCount <= 0)
	{
		InitialMaxPerPoint = 0;
		return false;
	}

	/*
	 * 260824 PPT 공식:
	 * 최대 배치 수 = Ceil(전체 캐릭터 수 / 전체 SpawnPoint 수)
	 */
	InitialMaxPerPoint = FMath::CeilToInt(
		static_cast<double>(InitialTotalCharacterCount)
		/ static_cast<double>(ValidSpawnPointCount));

	return InitialMaxPerPoint > 0;
}

bool USpawnSelectionComponent::SelectInitialSpawnTransform(
	FTransform& OutSpawnTransform,
	int32& OutSpawnPointIndex)
{
	OutSpawnTransform = FTransform::Identity;
	OutSpawnPointIndex = INDEX_NONE;

	if (!bInitialSpawnPrepared || InitialAssignmentsRemaining <= 0)
	{
		return false;
	}

	if (SpawnPoints.IsEmpty() || InitialSpawnCounts.Num() != SpawnPoints.Num())
	{
		return false;
	}

	// PPT 규칙대로 현재 배치 수가 최대 배치 수보다 작은 포인트만 후보로 사용합니다.
	TArray<int32> CandidateIndices;
	CandidateIndices.Reserve(SpawnPoints.Num());

	for (int32 Index = 0; Index < SpawnPoints.Num(); ++Index)
	{
		if (!IsValid(SpawnPoints[Index]))
		{
			continue;
		}

		if (InitialSpawnCounts[Index] < InitialMaxPerPoint)
		{
			CandidateIndices.Add(Index);
		}
	}

	if (CandidateIndices.IsEmpty())
	{
		UE_LOG(LogSpawnSelection, Warning,
			TEXT("SelectInitialSpawnTransform failed: no candidate remains. Remaining=%d"),
			InitialAssignmentsRemaining);
		return false;
	}

	// 등록 순서가 결과를 결정하지 않도록 후보 전체에서 Random 선택합니다.
	const int32 RandomCandidateIndex = FMath::RandRange(0, CandidateIndices.Num() - 1);
	const int32 SelectedSpawnIndex = CandidateIndices[RandomCandidateIndex];

	USceneComponent* SelectedSpawnPoint = SpawnPoints[SelectedSpawnIndex];
	if (!IsValid(SelectedSpawnPoint))
	{
		return false;
	}

	// 같은 프레임의 다음 Player/AI도 갱신된 배정 상태를 보도록 선택 즉시 반영합니다.
	InitialSpawnCounts[SelectedSpawnIndex]++;
	InitialAssignmentsRemaining--;

	OutSpawnTransform = SelectedSpawnPoint->GetComponentTransform();
	OutSpawnPointIndex = SelectedSpawnIndex;

	return true;
}

void USpawnSelectionComponent::ReleaseInitialSpawnSelection(
	const int32 SpawnPointIndex)
{
	if (!bInitialSpawnPrepared || !InitialSpawnCounts.IsValidIndex(SpawnPointIndex))
	{
		return;
	}

	if (InitialSpawnCounts[SpawnPointIndex] > 0)
	{
		InitialSpawnCounts[SpawnPointIndex]--;
		InitialAssignmentsRemaining++;
	}
}

bool USpawnSelectionComponent::IsInitialSpawnPrepared() const
{
	return bInitialSpawnPrepared;
}

int32 USpawnSelectionComponent::GetInitialAssignmentsRemaining() const
{
	return InitialAssignmentsRemaining;
}

int32 USpawnSelectionComponent::GetInitialTotalCharacterCount() const
{
	return InitialTotalCharacterCount;
}

bool USpawnSelectionComponent::SelectRespawnTransform(
	const FVector& DeathLocation,
	FTransform& OutSpawnTransform,
	int32& OutSpawnPointIndex)
{
	OutSpawnTransform = FTransform::Identity;
	OutSpawnPointIndex = INDEX_NONE;

	struct FRespawnDistanceEntry
	{
		int32 SpawnPointIndex = INDEX_NONE;
		double DistanceSquared = 0.0;
		double RandomTieBreaker = 0.0;
	};

	TArray<FRespawnDistanceEntry> SortedEntries;
	SortedEntries.Reserve(SpawnPoints.Num());

	for (int32 Index = 0; Index < SpawnPoints.Num(); ++Index)
	{
		if (!IsValid(SpawnPoints[Index]))
		{
			continue;
		}

		FRespawnDistanceEntry& Entry = SortedEntries.AddDefaulted_GetRef();
		Entry.SpawnPointIndex = Index;
		Entry.DistanceSquared = FVector::DistSquared(
			DeathLocation,
			SpawnPoints[Index]->GetComponentLocation());

		// 거리가 완전히 같은 포인트에서 등록 순서가 후보 경계를 결정하지 않게 합니다.
		Entry.RandomTieBreaker = FMath::FRand();
	}

	if (SortedEntries.IsEmpty())
	{
		return false;
	}

	SortedEntries.Sort(
		[](const FRespawnDistanceEntry& A, const FRespawnDistanceEntry& B)
		{
			if (A.DistanceSquared == B.DistanceSquared)
			{
				return A.RandomTieBreaker > B.RandomTieBreaker;
			}

			return A.DistanceSquared > B.DistanceSquared;
		});

	/*
	 * 260824 PPT 공식:
	 * 리스폰 후보 수 = Max(1, Floor(전체 SpawnPoint 수 / 2))
	 * int32 / 2가 내림 처리를 수행합니다.
	 */
	const int32 CandidateCount = FMath::Max(1, SortedEntries.Num() / 2);
	const int32 RandomCandidateIndex = FMath::RandRange(0, CandidateCount - 1);
	const int32 SelectedSpawnIndex = SortedEntries[RandomCandidateIndex].SpawnPointIndex;

	if (!SpawnPoints.IsValidIndex(SelectedSpawnIndex)
		|| !IsValid(SpawnPoints[SelectedSpawnIndex]))
	{
		return false;
	}

	OutSpawnTransform = SpawnPoints[SelectedSpawnIndex]->GetComponentTransform();
	OutSpawnPointIndex = SelectedSpawnIndex;

	return true;
}

bool USpawnSelectionComponent::SpawnInitialPawn(
	AController* Controller,
	TSubclassOf<APawn> PawnClass,
	APawn*& OutSpawnedPawn,
	FTransform& OutSpawnTransform,
	int32& OutSpawnPointIndex)
{
	OutSpawnedPawn = nullptr;
	OutSpawnTransform = FTransform::Identity;
	OutSpawnPointIndex = INDEX_NONE;

	if (!SelectInitialSpawnTransform(OutSpawnTransform, OutSpawnPointIndex))
	{
		UE_LOG(LogSpawnSelection, Warning,
			TEXT("SpawnInitialPawn failed: could not select an initial spawn point."));
		return false;
	}

	if (!SpawnPawnAtTransform(
		Controller,
		PawnClass,
		OutSpawnTransform,
		OutSpawnedPawn))
	{
		// SelectInitialSpawnTransform에서 선점한 배정 횟수를 자동 복구합니다.
		ReleaseInitialSpawnSelection(OutSpawnPointIndex);
		OutSpawnTransform = FTransform::Identity;
		OutSpawnPointIndex = INDEX_NONE;
		return false;
	}

	return true;
}

bool USpawnSelectionComponent::RespawnPawn(
	AController* Controller,
	TSubclassOf<APawn> PawnClass,
	const FVector& DeathLocation,
	APawn*& OutSpawnedPawn,
	FTransform& OutSpawnTransform,
	int32& OutSpawnPointIndex)
{
	OutSpawnedPawn = nullptr;
	OutSpawnTransform = FTransform::Identity;
	OutSpawnPointIndex = INDEX_NONE;

	if (!SelectRespawnTransform(
		DeathLocation,
		OutSpawnTransform,
		OutSpawnPointIndex))
	{
		UE_LOG(LogSpawnSelection, Warning,
			TEXT("RespawnPawn failed: could not select a respawn point."));
		return false;
	}

	return SpawnPawnAtTransform(
		Controller,
		PawnClass,
		OutSpawnTransform,
		OutSpawnedPawn);
}

bool USpawnSelectionComponent::SpawnPawnAtTransform(
	AController* Controller,
	TSubclassOf<APawn> PawnClass,
	const FTransform& SpawnTransform,
	APawn*& OutSpawnedPawn)
{
	OutSpawnedPawn = nullptr;

	if (!IsValid(Controller) || !PawnClass)
	{
		UE_LOG(LogSpawnSelection, Warning,
			TEXT("SpawnPawnAtTransform failed: Controller or PawnClass is invalid."));
		return false;
	}

	AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor) || !OwnerActor->HasAuthority())
	{
		UE_LOG(LogSpawnSelection, Warning,
			TEXT("SpawnPawnAtTransform rejected: spawn must be executed on Authority."));
		return false;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	// 현재 BP_QuakeGameMode Request Respawn의 동작과 동일하게 맞춥니다.
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* SpawnedPawn = World->SpawnActor<APawn>(
		PawnClass,
		SpawnTransform.GetLocation(),
		SpawnTransform.Rotator(),
		SpawnParameters);

	if (!IsValid(SpawnedPawn))
	{
		UE_LOG(LogSpawnSelection, Warning,
			TEXT("SpawnPawnAtTransform failed: SpawnActor returned null."));
		return false;
	}

	Controller->Possess(SpawnedPawn);

	if (Controller->GetPawn() != SpawnedPawn)
	{
		UE_LOG(LogSpawnSelection, Warning,
			TEXT("SpawnPawnAtTransform failed: Controller could not possess the spawned Pawn."));

		SpawnedPawn->Destroy();
		return false;
	}

	/*
	 * 스폰 시스템은 캐릭터의 게임플레이 상태를 초기화하지 않습니다.
	 * HP/Armor/Inventory/Buff, 이동 Velocity/Force 등의 초기화는
	 * Controller/Pawn 쪽 기존 시스템의 책임으로 남겨둡니다.
	 *
	 * 여기서는 SpawnPoint가 지정한 방향만 적용합니다.
	 */
	SpawnedPawn->SetActorRotation(SpawnTransform.Rotator());
	ApplySpawnRotation(Controller, SpawnTransform.Rotator());

	OutSpawnedPawn = SpawnedPawn;
	return true;
}

void USpawnSelectionComponent::ApplySpawnRotation(
	AController* Controller,
	const FRotator& SpawnRotation)
{
	if (!IsValid(Controller))
	{
		return;
	}

	// 서버 Controller / AIController의 ControlRotation을 먼저 초기화합니다.
	Controller->SetControlRotation(SpawnRotation);

	/*
	 * PlayerController는 실제 화면을 소유한 Client에도 회전을 적용해야 합니다.
	 * ClientSetRotation은 엔진에 이미 있는 Reliable Client RPC이므로
	 * BP_PlayerController에 별도 커스텀 RPC를 만들 필요가 없습니다.
	 */
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		PlayerController->ClientSetRotation(SpawnRotation, true);
	}
}
