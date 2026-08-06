#include "AI/Area/Internal/AreaRetreatPlanner.h"

#include "AIController.h"
#include "GameFramework/Pawn.h"

#include "AI/Area/AIAreaBase.h"
#include "AI/Area/AreaManagerSubsystem.h"

bool FAreaRetreatPlanner::FindBestRetreatPlan(
    UWorld& World,
    UAreaManagerSubsystem& AreaSubsystem,
    AAIController& ObserverController,
    APawn& ControlledPawn,
    const FAreaRetreatPlannerSettings& Settings,
    FAreaRetreatPlan& OutPlan)
{
    (void)World;
    OutPlan = FAreaRetreatPlan();

    // Runtime Area Graph는 Subsystem BeginPlay에서 준비합니다.
    // BT 계층에서는 그래프를 다시 만들지 않고 준비되지 않았다면 이번 판단만 실패시킵니다.
    if (!AreaSubsystem.IsGraphReady())
    {
        return false;
    }

    const FVector CurrentPosition = ControlledPawn.GetActorLocation();
    AAIAreaBase* CurrentArea = AreaSubsystem.GetAreaByPosition(CurrentPosition);
    if (!IsValid(CurrentArea))
    {
        return false;
    }

    const float CurrentAreaRisk = AreaSubsystem.GetAreaRiskScore(
        CurrentArea,
        &ObserverController);

    const float RequiredRiskImprovement = FMath::Max(
        0.0f,
        Settings.MinimumRiskImprovement);

    bool bHasBestRoute = false;
    AAIAreaBase* BestDestinationArea = nullptr;
    FVector BestDestinationNavPoint = FVector::ZeroVector;
    FAreaRouteResult BestRoute;

    const TArray<AAIAreaBase*> RegisteredAreas = AreaSubsystem.GetRegisteredAreas();
    for (AAIAreaBase* CandidateArea : RegisteredAreas)
    {
        if (!IsValid(CandidateArea)
            || CandidateArea == CurrentArea
            || !CandidateArea->IsAreaEnabled())
        {
            continue;
        }

        const float CandidateAreaRisk = AreaSubsystem.GetAreaRiskScore(
            CandidateArea,
            &ObserverController);

        // 후퇴는 현재 구역보다 실제로 안전한 구역으로만 수행합니다.
        if (CandidateAreaRisk >= CurrentAreaRisk - RequiredRiskImprovement)
        {
            continue;
        }

        FVector CandidateNavPoint = FVector::ZeroVector;
        if (!AreaSubsystem.GetBestBakedRetreatPoint(
                CandidateArea,
                CurrentPosition,
                CandidateNavPoint))
        {
            // 이전 Bake 데이터가 없는 맵도 완전히 멈추지 않도록 Area 내부의 가벼운 좌표를 사용합니다.
            // RebuildAreaGraph 후에는 에디터에서 Nav 검증된 지점이 항상 우선됩니다.
            CandidateNavPoint = CandidateArea->GetPointInsideTowards(
                CurrentPosition,
                FMath::Max(0.0f, Settings.CandidateInset));
        }

        FAreaRouteRequest RouteRequest;
        RouteRequest.StartPosition = CurrentPosition;
        RouteRequest.TargetPosition = CandidateNavPoint;
        RouteRequest.ObserverController = &ObserverController;
        RouteRequest.TraversalCapabilities = Settings.TraversalCapabilities;
        RouteRequest.bIncludeStartAreaRisk = false;
        RouteRequest.bAllowStraightLineFallback = false;
        RouteRequest.RiskEqualityTolerance = FMath::Max(
            0.0f,
            Settings.RiskEqualityTolerance);

        FAreaRouteResult CandidateRoute;
        if (!AreaSubsystem.FindSafestAreaRoute(RouteRequest, CandidateRoute)
            || !IsRouteExecutable(CandidateRoute))
        {
            continue;
        }

        if (IsCandidateRouteBetter(
                CandidateRoute,
                BestRoute,
                bHasBestRoute,
                Settings.RiskEqualityTolerance))
        {
            BestRoute = MoveTemp(CandidateRoute);
            BestDestinationArea = CandidateArea;
            BestDestinationNavPoint = CandidateNavPoint;
            bHasBestRoute = true;
        }
    }

    if (!bHasBestRoute)
    {
        return false;
    }

    OutPlan.CurrentArea = CurrentArea;
    OutPlan.DestinationArea = BestDestinationArea;
    OutPlan.DestinationNavPoint = BestDestinationNavPoint;
    OutPlan.CurrentAreaRisk = CurrentAreaRisk;
    OutPlan.RouteResult = MoveTemp(BestRoute);
    return true;
}

bool FAreaRetreatPlanner::IsRouteExecutable(
    const FAreaRouteResult& RouteResult)
{
    if (!RouteResult.bSuccess
        || RouteResult.RouteSteps.IsEmpty()
        || RouteResult.RouteAreas.Num() != RouteResult.RouteSteps.Num() + 1)
    {
        return false;
    }

    for (const FAreaRouteStep& RouteStep : RouteResult.RouteSteps)
    {
        if (!IsValid(RouteStep.FromArea.Get())
            || !IsValid(RouteStep.ToArea.Get()))
        {
            return false;
        }

        switch (RouteStep.TraversalType)
        {
        case EAreaTraversalType::Normal:
        case EAreaTraversalType::Jump:
        case EAreaTraversalType::Drop:
            // Jump/Drop은 별도 실행 Actor가 없어도 Exit 위치를 Goal Actor로 변환해 사용할 수 있습니다.
            break;

        case EAreaTraversalType::Teleport:
        case EAreaTraversalType::JumpPad:
        case EAreaTraversalType::Door:
            // 실제 상호작용 대상이 존재해야 하는 이동은 Actor가 반드시 필요합니다.
            if (!IsValid(RouteStep.TraversalActor.Get()))
            {
                return false;
            }
            break;

        default:
            return false;
        }
    }

    return true;
}

bool FAreaRetreatPlanner::IsCandidateRouteBetter(
    const FAreaRouteResult& CandidateRoute,
    const FAreaRouteResult& BestRoute,
    const bool bHasBestRoute,
    const float RiskEqualityTolerance)
{
    if (!bHasBestRoute)
    {
        return true;
    }

    const float Tolerance = FMath::Max(0.0f, RiskEqualityTolerance);

    if (CandidateRoute.TotalRisk < BestRoute.TotalRisk - Tolerance)
    {
        return true;
    }

    const bool bHasEqualRisk =
        FMath::Abs(CandidateRoute.TotalRisk - BestRoute.TotalRisk) <= Tolerance;

    return bHasEqualRisk
        && CandidateRoute.TotalTravelDistance < BestRoute.TotalTravelDistance;
}
