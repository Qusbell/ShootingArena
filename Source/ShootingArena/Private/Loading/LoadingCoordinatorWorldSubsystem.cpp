#include "Loading/LoadingCoordinatorWorldSubsystem.h"

#include "Loading/LoadingCoordinator.h"
#include "Engine/World.h"

void ULoadingCoordinatorWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_Client || InWorld.IsPreviewWorld()) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<ALoadingCoordinator>(ALoadingCoordinator::StaticClass(), FTransform::Identity, Params);
}
