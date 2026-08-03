#include "AI/Area/Internal/AreaRouteFinder.h"

#include "AI/Area/AIAreaBase.h"
#include "AI/Area/AreaLinkBase.h"
#include "AI/Area/Internal/AreaGraphService.h"
#include "AI/Area/Internal/AreaRiskService.h"
#include "Algo/Reverse.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationSystem.h"

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

    struct FPathCacheKey
    {
        FIntVector Start;
        FIntVector End;

        bool operator==(const FPathCacheKey& Other) const
        {
            return Start == Other.Start && End == Other.End;
        }
    };

    uint32 GetTypeHash(const FPathCacheKey& Key)
    {
        return HashCombine(GetTypeHash(Key.Start), GetTypeHash(Key.End));
    }

    static FIntVector QuantizePoint(const FVector& Point)
    {
        // 1cm 단위로 양자화해 같은 요청 안의 중복 NavMesh 계산을 캐시합니다.
        return FIntVector(
            FMath::RoundToInt(Point.X),
            FMath::RoundToInt(Point.Y),
            FMath::RoundToInt(Point.Z));
    }

    static bool IsFiniteScore(const FRouteScore& Score)
    {
        return Score.Risk < TNumericLimits<double>::Max()
            && Score.Distance < TNumericLimits<double>::Max();
    }

    static bool IsBetterScore(const FRouteScore& Candidate, const FRouteScore& Current, const double RiskTolerance)
    {
        if (!IsFiniteScore(Current))
        {
            return true;
        }

        if (Candidate.Risk < Current.Risk - RiskTolerance)
        {
            return true;
        }

        if (FMath::Abs(Candidate.Risk - Current.Risk) <= RiskTolerance
            && Candidate.Distance < Current.Distance)
        {
            return true;
        }

        return false;
    }

    static bool TryGetTravelDistance(
        UWorld* World,
        const FVector& Start,
        const FVector& End,
        const bool bAllowStraightLineFallback,
        TMap<FPathCacheKey, double>& Cache,
        double& OutDistance)
    {
        const FPathCacheKey Key{QuantizePoint(Start), QuantizePoint(End)};
        if (const double* Cached = Cache.Find(Key))
        {
            OutDistance = *Cached;
            return true;
        }

        double PathLength = 0.0;
        const ENavigationQueryResult::Type Result = UNavigationSystemV1::GetPathLength(
            World,
            Start,
            End,
            PathLength,
            nullptr,
            TSubclassOf<UNavigationQueryFilter>());

        if (Result == ENavigationQueryResult::Success)
        {
            Cache.Add(Key, PathLength);
            OutDistance = PathLength;
            return true;
        }

        if (bAllowStraightLineFallback)
        {
            const double StraightDistance = FVector::Distance(Start, End);
            Cache.Add(Key, StraightDistance);
            OutDistance = StraightDistance;
            return true;
        }

        return false;
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

bool FAreaRouteFinder::FindSafestRoute(const FAreaRouteRequest& Request, FAreaRouteResult& OutResult) const
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

    // State 0은 실제 AI 시작 위치입니다.
    // State N+1은 Connections[N]을 통과한 뒤 ExitLocation에 도착한 상태입니다.
    TArray<FSearchState> States;
    States.SetNum(Connections.Num() + 1);

    States[0].Area = StartArea;
    States[0].ArrivalLocation = Request.StartPosition;
    States[0].BestScore.Risk = Request.bIncludeStartAreaRisk
        ? RiskService.GetAreaRiskScore(StartArea, Request.ObserverController)
        : 0.0;
    States[0].BestScore.Distance = 0.0;

    for (int32 ConnectionIndex = 0; ConnectionIndex < Connections.Num(); ++ConnectionIndex)
    {
        States[ConnectionIndex + 1].Area = Connections[ConnectionIndex].ToArea;
        States[ConnectionIndex + 1].ArrivalLocation = Connections[ConnectionIndex].ExitLocation;
    }

    FRouteScore BestGoalScore;
    int32 BestGoalStateId = INDEX_NONE;
    TMap<FPathCacheKey, double> PathDistanceCache;

    while (true)
    {
        int32 CurrentStateId = INDEX_NONE;
        FRouteScore CurrentBest;

        // 연결 수가 많지 않은 Area 그래프이므로 단순 선형 선택을 사용합니다.
        // 추후 수백 개 이상으로 커지면 Heap 기반 우선순위 큐로 교체할 수 있습니다.
        for (int32 StateId = 0; StateId < States.Num(); ++StateId)
        {
            const FSearchState& CandidateState = States[StateId];
            if (CandidateState.bVisited || !IsFiniteScore(CandidateState.BestScore))
            {
                continue;
            }

            if (CurrentStateId == INDEX_NONE
                || IsBetterScore(CandidateState.BestScore, CurrentBest, Request.RiskEqualityTolerance))
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

        // 목표 Area에 도착한 모든 상태에서 실제 목표 위치까지의 마지막 보행 거리를 비교합니다.
        if (CurrentArea == TargetArea)
        {
            double FinalWalkDistance = 0.0;
            if (TryGetTravelDistance(
                WorldPtr,
                CurrentState.ArrivalLocation,
                Request.TargetPosition,
                Request.bAllowStraightLineFallback,
                PathDistanceCache,
                FinalWalkDistance))
            {
                FRouteScore GoalCandidate = CurrentState.BestScore;
                GoalCandidate.Distance += FinalWalkDistance;

                if (BestGoalStateId == INDEX_NONE
                    || IsBetterScore(GoalCandidate, BestGoalScore, Request.RiskEqualityTolerance))
                {
                    BestGoalStateId = CurrentStateId;
                    BestGoalScore = GoalCandidate;
                }
            }
        }

        const TArray<int32>& OutgoingIndices = GraphService.GetOutgoingConnectionIndices(CurrentArea);
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

            double WalkDistance = 0.0;
            bool bHasWalkPath = false;

            if (Connection.TraversalType == EAreaTraversalType::Normal)
            {
                // Normal은 현재 위치부터 ToArea 안쪽 Exit까지 전체 보행 경로 길이를 사용합니다.
                bHasWalkPath = TryGetTravelDistance(
                    WorldPtr,
                    CurrentState.ArrivalLocation,
                    Connection.ExitLocation,
                    Request.bAllowStraightLineFallback,
                    PathDistanceCache,
                    WalkDistance);
            }
            else
            {
                // 특수 Link는 현재 위치부터 입구까지 걸은 뒤 Entry -> Exit 이동을 수행합니다.
                bHasWalkPath = TryGetTravelDistance(
                    WorldPtr,
                    CurrentState.ArrivalLocation,
                    Connection.EntryLocation,
                    Request.bAllowStraightLineFallback,
                    PathDistanceCache,
                    WalkDistance);
            }

            if (!bHasWalkPath)
            {
                continue;
            }

            FRouteScore CandidateScore = CurrentState.BestScore;
            CandidateScore.Distance += WalkDistance + FMath::Max(0.0f, Connection.TraversalDistanceCost);
            CandidateScore.Risk += RiskService.GetAreaRiskScore(Connection.ToArea.Get(), Request.ObserverController);
            CandidateScore.Risk += FMath::Max(0.0f, Connection.TraversalRiskCost);

            if (IsBetterScore(CandidateScore, NextState.BestScore, Request.RiskEqualityTolerance))
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
        OutResult.FailureReason = FText::FromString(TEXT("목표 Area까지 사용할 수 있는 경로를 찾지 못했습니다."));
        return false;
    }

    TArray<int32> ReverseConnectionIndices;
    int32 TraceStateId = BestGoalStateId;

    while (TraceStateId != 0 && States.IsValidIndex(TraceStateId))
    {
        const FSearchState& TraceState = States[TraceStateId];
        if (TraceState.ViaConnectionIndex == INDEX_NONE || TraceState.ParentStateId == INDEX_NONE)
        {
            OutResult.FailureReason = FText::FromString(TEXT("경로 역추적 중 연결 정보가 끊어졌습니다."));
            return false;
        }

        ReverseConnectionIndices.Add(TraceState.ViaConnectionIndex);
        TraceStateId = TraceState.ParentStateId;
    }

    Algo::Reverse(ReverseConnectionIndices);

    OutResult.RouteAreas.Add(StartArea);
    for (const int32 ConnectionIndex : ReverseConnectionIndices)
    {
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
