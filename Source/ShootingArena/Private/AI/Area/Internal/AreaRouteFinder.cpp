#include "AI/Area/Internal/AreaRouteFinder.h"

#include "AI/Area/AIAreaBase.h"
#include "AI/Area/AreaLinkBase.h"
#include "AI/Area/Internal/AreaGraphService.h"
#include "AI/Area/Internal/AreaRiskService.h"
#include "Algo/Reverse.h"

namespace AreaRouteFinderPrivate
{
    struct FRouteScore
    {
        double Risk = TNumericLimits<double>::Max();
        double Distance = TNumericLimits<double>::Max();
    };

    struct FSearchState
    {
        TWeakObjectPtr<AAIAreaBase> Area;
        FVector ArrivalLocation = FVector::ZeroVector;
        FRouteScore BestScore;
        int32 ParentStateId = INDEX_NONE;
        int32 ViaConnectionIndex = INDEX_NONE;
        bool bVisited = false;
    };

    static bool IsFiniteScore(const FRouteScore& Score)
    {
        return Score.Risk < TNumericLimits<double>::Max()
            && Score.Distance < TNumericLimits<double>::Max();
    }

    static bool IsBetterScore(
        const FRouteScore& Candidate,
        const FRouteScore& Current,
        const double RiskTolerance)
    {
        if (!IsFiniteScore(Current))
        {
            return true;
        }

        if (Candidate.Risk < Current.Risk - RiskTolerance)
        {
            return true;
        }

        return FMath::Abs(Candidate.Risk - Current.Risk) <= RiskTolerance
            && Candidate.Distance < Current.Distance;
    }

    /**
     * 런타임 Area 판단에서는 NavigationSystem을 호출하지 않습니다.
     * 고정된 연결 사이 구간은 에디터에서 저장한 Nav 거리 캐시를 사용하고,
     * 실제 AI 시작점처럼 미리 고정할 수 없는 첫 구간만 직선거리로 추정합니다.
     */
    static bool TryGetWalkDistance(
        const FAreaGraphService& GraphService,
        const TArray<FAreaDirectedConnection>& Connections,
        const FSearchState& CurrentState,
        const int32 NextConnectionIndex,
        const FVector& WalkTarget,
        double& OutDistance)
    {
        OutDistance = 0.0;

        if (!Connections.IsValidIndex(NextConnectionIndex))
        {
            return false;
        }

        // State 0은 움직이는 실제 AI 위치이므로 에디터 캐싱이 불가능합니다.
        if (CurrentState.ViaConnectionIndex == INDEX_NONE)
        {
            OutDistance = FVector::Distance(CurrentState.ArrivalLocation, WalkTarget);
            return true;
        }

        if (!Connections.IsValidIndex(CurrentState.ViaConnectionIndex))
        {
            return false;
        }

        const FAreaDirectedConnection& PreviousConnection =
            Connections[CurrentState.ViaConnectionIndex];
        const FAreaDirectedConnection& NextConnection =
            Connections[NextConnectionIndex];

        float CachedDistance = 0.0f;
        bool bCachedReachable = false;
        const bool bHasCache = GraphService.TryGetBakedTransitionDistance(
            PreviousConnection.BakedConnectionIndex,
            NextConnection.BakedConnectionIndex,
            CachedDistance,
            bCachedReachable);

        if (bHasCache)
        {
            if (!bCachedReachable)
            {
                return false;
            }

            OutDistance = CachedDistance;
            return true;
        }

        // 기존 맵을 Rebuild하기 전에도 기능이 완전히 멈추지 않도록 가벼운 호환 Fallback을 사용합니다.
        // 이 경로에서도 Nav 조회는 발생하지 않으며, Rebuild 후에는 저장된 실제 Nav 거리가 우선됩니다.
        OutDistance = FVector::Distance(CurrentState.ArrivalLocation, WalkTarget);
        return true;
    }
}

FAreaRouteFinder::FAreaRouteFinder(
    UWorld* InWorld,
    const FAreaGraphService& InGraphService,
    const FAreaRiskService& InRiskService)
    : World(InWorld)
    , GraphService(InGraphService)
    , RiskService(InRiskService)
{
}

bool FAreaRouteFinder::FindSafestRoute(
    const FAreaRouteRequest& Request,
    FAreaRouteResult& OutResult) const
{
    return FindRouteInternal(Request, OutResult, true);
}

bool FAreaRouteFinder::FindRoute(
    const FAreaRouteRequest& Request,
    FAreaRouteResult& OutResult) const
{
    return FindRouteInternal(Request, OutResult, false);
}

bool FAreaRouteFinder::FindRouteInternal(
    const FAreaRouteRequest& Request,
    FAreaRouteResult& OutResult,
    const bool bUseRisk) const
{
    using namespace AreaRouteFinderPrivate;

    OutResult.Reset();

    UWorld* WorldPtr = World.Get();
    if (!IsValid(WorldPtr))
    {
        OutResult.FailureReason = FText::FromString(TEXT("유효한 World가 없습니다."));
        return false;
    }

    AAIAreaBase* StartArea = GraphService.FindAreaByPosition(Request.StartPosition);
    if (!IsValid(StartArea))
    {
        OutResult.FailureReason = FText::FromString(TEXT("시작 위치를 포함하는 Area를 찾지 못했습니다."));
        return false;
    }

    AAIAreaBase* TargetArea = GraphService.FindAreaByPosition(Request.TargetPosition);
    if (!IsValid(TargetArea))
    {
        OutResult.FailureReason = FText::FromString(TEXT("목표 위치를 포함하는 Area를 찾지 못했습니다."));
        return false;
    }

    const TArray<FAreaDirectedConnection>& Connections = GraphService.GetConnections();

    // 위험도 경로에서는 한 번의 탐색 안에서 같은 Area 위험도를 반복 계산하지 않습니다.
    // 순수 거리 경로에서는 아래 캐시와 RiskService를 실제로 사용하지 않습니다.
    TMap<const AAIAreaBase*, double> AreaRiskCache;
    const auto GetCachedAreaRisk =
        [&AreaRiskCache, &Request, bUseRisk, this](AAIAreaBase* Area) -> double
        {
            // FindAreaRoute에서는 위험도 Service 자체를 호출하지 않습니다.
            if (!bUseRisk || !IsValid(Area))
            {
                return 0.0;
            }

            if (const double* CachedRisk = AreaRiskCache.Find(Area))
            {
                return *CachedRisk;
            }

            const double Risk = RiskService.GetAreaRiskScore(
                Area,
                Request.ObserverController);
            AreaRiskCache.Add(Area, Risk);
            return Risk;
        };

    // State 0은 실제 AI 시작 위치입니다.
    // State N+1은 Connections[N]을 통과한 뒤 ExitLocation에 도착한 상태입니다.
    TArray<FSearchState> States;
    States.SetNum(Connections.Num() + 1);

    States[0].Area = StartArea;
    States[0].ArrivalLocation = Request.StartPosition;
    States[0].BestScore.Risk = bUseRisk && Request.bIncludeStartAreaRisk
        ? GetCachedAreaRisk(StartArea)
        : 0.0;
    States[0].BestScore.Distance = 0.0;

    for (int32 ConnectionIndex = 0; ConnectionIndex < Connections.Num(); ++ConnectionIndex)
    {
        States[ConnectionIndex + 1].Area = Connections[ConnectionIndex].ToArea;
        States[ConnectionIndex + 1].ArrivalLocation = Connections[ConnectionIndex].ExitLocation;
    }

    FRouteScore BestGoalScore;
    int32 BestGoalStateId = INDEX_NONE;

    while (true)
    {
        int32 CurrentStateId = INDEX_NONE;
        FRouteScore CurrentBest;

        for (int32 StateId = 0; StateId < States.Num(); ++StateId)
        {
            const FSearchState& CandidateState = States[StateId];
            if (CandidateState.bVisited || !IsFiniteScore(CandidateState.BestScore))
            {
                continue;
            }

            if (CurrentStateId == INDEX_NONE
                || IsBetterScore(
                    CandidateState.BestScore,
                    CurrentBest,
                    Request.RiskEqualityTolerance))
            {
                CurrentStateId = StateId;
                CurrentBest = CandidateState.BestScore;
            }
        }

        if (CurrentStateId == INDEX_NONE)
        {
            break;
        }

        FSearchState& CurrentState = States[CurrentStateId];
        CurrentState.bVisited = true;

        AAIAreaBase* CurrentArea = CurrentState.Area.Get();
        if (!IsValid(CurrentArea))
        {
            continue;
        }

        // 마지막 목표는 런타임에 달라질 수 있어 캐싱할 수 없으므로 가벼운 직선거리만 사용합니다.
        if (CurrentArea == TargetArea)
        {
            FRouteScore GoalCandidate = CurrentState.BestScore;
            GoalCandidate.Distance += FVector::Distance(
                CurrentState.ArrivalLocation,
                Request.TargetPosition);

            if (BestGoalStateId == INDEX_NONE
                || IsBetterScore(
                    GoalCandidate,
                    BestGoalScore,
                    Request.RiskEqualityTolerance))
            {
                BestGoalStateId = CurrentStateId;
                BestGoalScore = GoalCandidate;
            }
        }

        const TArray<int32>& OutgoingIndices =
            GraphService.GetOutgoingConnectionIndices(CurrentArea);

        for (const int32 ConnectionIndex : OutgoingIndices)
        {
            if (!Connections.IsValidIndex(ConnectionIndex))
            {
                continue;
            }

            const FAreaDirectedConnection& Connection = Connections[ConnectionIndex];
            if (!Connection.bEnabled
                || !Connection.FromArea.IsValid()
                || !Connection.ToArea.IsValid()
                || !Request.TraversalCapabilities.Supports(Connection.TraversalType))
            {
                continue;
            }

            const int32 NextStateId = ConnectionIndex + 1;
            FSearchState& NextState = States[NextStateId];

            const FVector WalkTarget = Connection.TraversalType == EAreaTraversalType::Normal
                ? Connection.ExitLocation
                : Connection.EntryLocation;

            double WalkDistance = 0.0;
            if (!TryGetWalkDistance(
                GraphService,
                Connections,
                CurrentState,
                ConnectionIndex,
                WalkTarget,
                WalkDistance))
            {
                continue;
            }

            FRouteScore CandidateScore = CurrentState.BestScore;
            CandidateScore.Distance += WalkDistance
                + FMath::Max(0.0f, Connection.TraversalDistanceCost);
            if (bUseRisk)
            {
                CandidateScore.Risk += GetCachedAreaRisk(Connection.ToArea.Get());
                CandidateScore.Risk += FMath::Max(0.0f, Connection.TraversalRiskCost);
            }

            if (IsBetterScore(
                CandidateScore,
                NextState.BestScore,
                Request.RiskEqualityTolerance))
            {
                NextState.BestScore = CandidateScore;
                NextState.ParentStateId = CurrentStateId;
                NextState.ViaConnectionIndex = ConnectionIndex;
                NextState.bVisited = false;
            }
        }
    }

    if (BestGoalStateId == INDEX_NONE)
    {
        OutResult.FailureReason =
            FText::FromString(TEXT("목표 Area까지 사용할 수 있는 경로를 찾지 못했습니다."));
        return false;
    }

    TArray<int32> ReverseConnectionIndices;
    int32 TraceStateId = BestGoalStateId;

    while (TraceStateId != 0 && States.IsValidIndex(TraceStateId))
    {
        const FSearchState& TraceState = States[TraceStateId];
        if (TraceState.ViaConnectionIndex == INDEX_NONE
            || TraceState.ParentStateId == INDEX_NONE)
        {
            OutResult.FailureReason =
                FText::FromString(TEXT("경로 역추적 중 연결 정보가 끊어졌습니다."));
            return false;
        }

        ReverseConnectionIndices.Add(TraceState.ViaConnectionIndex);
        TraceStateId = TraceState.ParentStateId;
    }

    Algo::Reverse(ReverseConnectionIndices);

    OutResult.RouteAreas.Add(StartArea);
    for (const int32 ConnectionIndex : ReverseConnectionIndices)
    {
        if (!Connections.IsValidIndex(ConnectionIndex))
        {
            OutResult.FailureReason =
                FText::FromString(TEXT("경로 결과에 유효하지 않은 연결이 포함되었습니다."));
            return false;
        }

        const FAreaDirectedConnection& Connection = Connections[ConnectionIndex];

        FAreaRouteStep Step;
        Step.FromArea = Connection.FromArea.Get();
        Step.ToArea = Connection.ToArea.Get();
        Step.SourceLink = Connection.SourceLink.Get();
        Step.TraversalActor = Connection.TraversalActor.Get();
        Step.TraversalType = Connection.TraversalType;
        Step.EntryLocation = Connection.EntryLocation;
        Step.ExitLocation = Connection.ExitLocation;
        Step.TraversalDistanceCost = Connection.TraversalDistanceCost;
        Step.TraversalRiskCost = Connection.TraversalRiskCost;
        OutResult.RouteSteps.Add(Step);
        OutResult.RouteAreas.Add(Connection.ToArea.Get());
    }

    OutResult.TotalRisk = static_cast<float>(BestGoalScore.Risk);
    OutResult.TotalTravelDistance = static_cast<float>(BestGoalScore.Distance);
    OutResult.bSuccess = true;
    OutResult.FailureReason = FText::GetEmpty();
    return true;
}
