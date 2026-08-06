#pragma once

#include "CoreMinimal.h"
#include "AI/Area/AreaTypes.h"

class UWorld;
class FAreaGraphService;
class FAreaRiskService;

/** 위험도 기반 경로와 순수 거리 기반 경로 탐색을 함께 담당합니다. */
class FAreaRouteFinder
{
public:
    FAreaRouteFinder(UWorld* InWorld, const FAreaGraphService& InGraphService, const FAreaRiskService& InRiskService);

    /** 위험도 우선, 거리 차선으로 경로를 계산합니다. */
    bool FindSafestRoute(const FAreaRouteRequest& Request, FAreaRouteResult& OutResult) const;

    /** Area/Traversal 위험도를 완전히 제외하고 이동거리만으로 최단 경로를 계산합니다. */
    bool FindRoute(const FAreaRouteRequest& Request, FAreaRouteResult& OutResult) const;

private:
    /** 두 공개 경로 함수가 공유하는 실제 탐색 구현입니다. */
    bool FindRouteInternal(
        const FAreaRouteRequest& Request,
        FAreaRouteResult& OutResult,
        bool bUseRisk) const;

    TWeakObjectPtr<UWorld> World;
    const FAreaGraphService& GraphService;
    const FAreaRiskService& RiskService;
};
