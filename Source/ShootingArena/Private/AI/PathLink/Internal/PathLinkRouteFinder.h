#pragma once

#include "CoreMinimal.h"
#include "AI/PathLink/PathLinkTypes.h"

class AActor;
class APathLink;
class UWorld;

/**
 * UPathLinkSubsystem에서만 사용하는 순수 C++ 최단 경로 계산기입니다.
 * Area / Risk와 전혀 관계없이 NavMesh 거리와 PathLink 거리만 사용합니다.
 */
class FPathLinkRouteFinder
{
public:
    explicit FPathLinkRouteFinder(UWorld* InWorld)
        : World(InWorld)
    {
    }

    bool FindShortestRoute(
        const FVector& StartLocation,
        const FVector& TargetLocation,
        const TArray<APathLink*>& Links,
        AActor* PathfindingContext,
        FPathLinkRouteResult& OutResult) const;

private:
    UWorld* World = nullptr;
};
