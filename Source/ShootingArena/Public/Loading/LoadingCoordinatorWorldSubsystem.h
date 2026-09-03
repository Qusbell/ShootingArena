#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LoadingCoordinatorWorldSubsystem.generated.h"

/** 각 게임 월드의 서버에서 ALoadingCoordinator를 한 번 자동 생성합니다. */
UCLASS()
class SHOOTINGARENA_API ULoadingCoordinatorWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
