#pragma once

#include "CoreMinimal.h"
#include "AI/Area/AreaTypes.h"

class UWorld;
class FAreaGraphService;
class FAreaRiskService;

/** 위험도 우선, 거리 차선의 전체 Area 경로 탐색을 담당합니다. */
class FAreaRouteFinder
{
public:
    FAreaRouteFinder(UWorld* InWorld, const FAreaGraphService& InGraphService, const FAreaRiskService& InRiskService);

    bool FindSafestRoute(const FAreaRouteRequest& Request, FAreaRouteResult& OutResult) const;

private:
    TWeakObjectPtr<UWorld> World;
    const FAreaGraphService& GraphService;
    const FAreaRiskService& RiskService;
};
