#pragma once

#include "CoreMinimal.h"
#include "AI/Area/AreaTypes.h"

class AAIController;
class AAIAreaBase;
class APawn;
class UAreaManagerSubsystem;
class UWorld;

/** 후퇴 경로 탐색에 사용하는 공통 설정입니다. */
struct FAreaRetreatPlannerSettings
{
    FAreaTraversalCapabilities TraversalCapabilities;
    float MinimumRiskImprovement = 0.01f;
    float RiskEqualityTolerance = 0.01f;
    FVector NavProjectionExtent = FVector(300.0, 300.0, 500.0);
    float CandidateInset = 80.0f;
};

/** 후퇴 경로 탐색 결과입니다. */
struct FAreaRetreatPlan
{
    TWeakObjectPtr<AAIAreaBase> CurrentArea;
    TWeakObjectPtr<AAIAreaBase> DestinationArea;
    FVector DestinationNavPoint = FVector::ZeroVector;
    float CurrentAreaRisk = 0.0f;
    FAreaRouteResult RouteResult;
};

/**
 * BT Service와 BT Task가 같은 기준으로 후퇴 경로를 판단하도록 묶어 둔 내부 계산기입니다.
 * 실제 이동은 수행하지 않고 가장 안전한 도달 가능 경로만 반환합니다.
 */
class FAreaRetreatPlanner
{
public:
    static bool FindBestRetreatPlan(
        UWorld& World,
        UAreaManagerSubsystem& AreaSubsystem,
        AAIController& ObserverController,
        APawn& ControlledPawn,
        const FAreaRetreatPlannerSettings& Settings,
        FAreaRetreatPlan& OutPlan);

private:
    static bool IsRouteExecutable(const FAreaRouteResult& RouteResult);

    static bool IsCandidateRouteBetter(
        const FAreaRouteResult& CandidateRoute,
        const FAreaRouteResult& BestRoute,
        bool bHasBestRoute,
        float RiskEqualityTolerance);
};
