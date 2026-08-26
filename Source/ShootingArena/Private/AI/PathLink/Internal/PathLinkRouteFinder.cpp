#include "AI/PathLink/Internal/PathLinkRouteFinder.h"

#include "AI/PathLink/PathLink.h"
#include "AI/PathLink/PathLinkSettings.h"
#include "Algo/Reverse.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathLinkRouteFinder, Log, All);

namespace
{
    const FVector NavProjectionExtent(150.0, 150.0, 400.0);

    enum class EDynamicRouteEdgeType : uint8
    {
        DirectToTarget,
        EnterTraversal,
        TraversalToTarget
    };

    struct FDynamicRouteEdge
    {
        int32 From = INDEX_NONE;
        int32 To = INDEX_NONE;

        /** Dijkstra 비교용 전체 비용입니다. EnterTraversal이면 Normal + LinkCost입니다. */
        double Cost = 0.0;

        /** 이 Edge 안에서 실제 NavMesh 이동에 해당하는 거리입니다. */
        double NavDistance = 0.0;

        EDynamicRouteEdgeType Type = EDynamicRouteEdgeType::DirectToTarget;

        /** EnterTraversal일 때 새로 진입하는 Traversal Index입니다. */
        int32 TraversalIndex = INDEX_NONE;
    };

    bool ProjectToNavigation(
        UWorld* World,
        const FVector& SourceLocation,
        FVector& OutNavLocation)
    {
        OutNavLocation = SourceLocation;

        UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World);
        if (!IsValid(NavSystem))
        {
            return false;
        }

        FNavLocation ProjectedLocation;
        if (!NavSystem->ProjectPointToNavigation(
            SourceLocation,
            ProjectedLocation,
            NavProjectionExtent,
            static_cast<const FNavAgentProperties*>(nullptr),
            FSharedConstNavQueryFilter()))
        {
            return false;
        }

        OutNavLocation = ProjectedLocation.Location;
        return true;
    }

    /**
     * 후보 비교용 거리만 계산합니다. PathPoints를 복사하지 않아 Graph 구축/Route 비교 시 메모리 비용을 줄입니다.
     */
    bool BuildNavigationDistance(
        UWorld* World,
        const FVector& FromNavLocation,
        const FVector& ToNavLocation,
        AActor* PathfindingContext,
        double& OutDistance,
        int32* InOutNavQueryCount = nullptr)
    {
        OutDistance = 0.0;

        if (FromNavLocation.Equals(ToNavLocation, 1.0f))
        {
            return true;
        }

        if (InOutNavQueryCount)
        {
            ++(*InOutNavQueryCount);
        }

        UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
            World,
            FromNavLocation,
            ToNavLocation,
            PathfindingContext,
            nullptr);

        if (!IsValid(Path) || !Path->IsValid() || Path->IsPartial())
        {
            return false;
        }

        OutDistance = Path->GetPathLength();
        return true;
    }

    /** 최종 선택된 Normal Segment에 대해서만 실제 PathPoints를 만듭니다. */
    bool BuildNavigationPath(
        UWorld* World,
        const FVector& FromNavLocation,
        const FVector& ToNavLocation,
        AActor* PathfindingContext,
        double& OutDistance,
        TArray<FVector>& OutPathPoints,
        int32* InOutNavQueryCount = nullptr)
    {
        OutDistance = 0.0;
        OutPathPoints.Reset();

        if (FromNavLocation.Equals(ToNavLocation, 1.0f))
        {
            OutPathPoints.Add(FromNavLocation);
            OutPathPoints.Add(ToNavLocation);
            return true;
        }

        if (InOutNavQueryCount)
        {
            ++(*InOutNavQueryCount);
        }

        UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
            World,
            FromNavLocation,
            ToNavLocation,
            PathfindingContext,
            nullptr);

        if (!IsValid(Path) || !Path->IsValid() || Path->IsPartial())
        {
            return false;
        }

        OutDistance = Path->GetPathLength();
        OutPathPoints = Path->PathPoints;
        return true;
    }

    double GetLinkTraversalCost(
        const APathLink* Link,
        const FVector& EntryLocation,
        const FVector& ExitLocation)
    {
        if (!IsValid(Link))
        {
            return 0.0;
        }

        // Teleport는 순간이동 자체의 물리적 이동거리 Cost를 0으로 취급합니다.
        if (Link->GetLinkType() == EPathLinkType::Teleport)
        {
            return 0.0;
        }

        return FVector::Distance(EntryLocation, ExitLocation);
    }
}

bool FPathLinkRouteFinder::BuildStaticGraph(
    const TArray<APathLink*>& Links,
    AActor* PathfindingContext,
    FPathLinkStaticGraph& OutGraph) const
{
    OutGraph.Reset();

    if (!IsValid(World))
    {
        return false;
    }

    UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World);
    if (!IsValid(NavSystem))
    {
        return false;
    }

    const double BuildStartSeconds = FPlatformTime::Seconds();

    OutGraph.Traversals.Reserve(Links.Num() * 2);

    for (APathLink* Link : Links)
    {
        // Subsystem이 구조 Validation/Enabled 필터를 이미 끝낸 Link만 전달합니다.
        // 여기서는 Hot Path의 중복 Validation을 다시 수행하지 않습니다.
        if (!IsValid(Link) || !Link->IsEnabled())
        {
            continue;
        }

        const FVector ForwardEntry = Link->GetEntryLocation();
        const FVector ForwardExit = Link->GetExitLocation();

        if (ForwardEntry.ContainsNaN()
            || ForwardExit.ContainsNaN()
            || ForwardEntry.Equals(ForwardExit, 1.0f))
        {
            continue;
        }

        FVector ForwardEntryNav;
        FVector ForwardExitNav;
        if (!ProjectToNavigation(World, ForwardEntry, ForwardEntryNav)
            || !ProjectToNavigation(World, ForwardExit, ForwardExitNav))
        {
            continue;
        }

        auto AddTraversal = [&OutGraph, Link](
            const bool Reverse,
            const FVector& EntryLocation,
            const FVector& ExitLocation,
            const FVector& EntryNavLocation,
            const FVector& ExitNavLocation)
        {
            FPathLinkTraversalNode& Traversal = OutGraph.Traversals.AddDefaulted_GetRef();
            Traversal.Link = Link;
            Traversal.Reverse = Reverse;
            Traversal.LinkType = Link->GetLinkType();
            Traversal.EntryLocation = EntryLocation;
            Traversal.ExitLocation = ExitLocation;
            Traversal.EntryNavLocation = EntryNavLocation;
            Traversal.ExitNavLocation = ExitNavLocation;
            Traversal.LinkCost = GetLinkTraversalCost(Link, EntryLocation, ExitLocation);
        };

        AddTraversal(
            false,
            ForwardEntry,
            ForwardExit,
            ForwardEntryNav,
            ForwardExitNav);

        if (Link->IsTwoWay())
        {
            // TwoWay는 같은 두 Endpoint를 반대로 사용하는 별도 Traversal로 캐시합니다.
            AddTraversal(
                true,
                ForwardExit,
                ForwardEntry,
                ForwardExitNav,
                ForwardEntryNav);
        }
    }

    const int32 TraversalCount = OutGraph.Traversals.Num();
    OutGraph.Transitions.SetNum(TraversalCount * TraversalCount);

    int32 NavQueryCount = 0;

    // 핵심 최적화 지점입니다.
    // Link Exit -> 다른 Link Entry는 Start/Target과 무관한 정적 값이므로 Graph 구축 시 한 번만 계산합니다.
    for (int32 FromIndex = 0; FromIndex < TraversalCount; ++FromIndex)
    {
        const FPathLinkTraversalNode& FromTraversal = OutGraph.Traversals[FromIndex];

        for (int32 ToIndex = 0; ToIndex < TraversalCount; ++ToIndex)
        {
            if (FromIndex == ToIndex)
            {
                continue;
            }

            const FPathLinkTraversalNode& ToTraversal = OutGraph.Traversals[ToIndex];

            // 같은 Link를 즉시 반대 방향으로 다시 타는 순환은 비음수 Cost 최단경로에서 이득이 없으므로 제외합니다.
            if (FromTraversal.Link == ToTraversal.Link)
            {
                continue;
            }

            double NavDistance = 0.0;
            if (!BuildNavigationDistance(
                World,
                FromTraversal.ExitNavLocation,
                ToTraversal.EntryNavLocation,
                PathfindingContext,
                NavDistance,
                &NavQueryCount))
            {
                continue;
            }

            const int32 TransitionIndex = FromIndex * TraversalCount + ToIndex;
            FPathLinkCachedTransition& Transition = OutGraph.Transitions[TransitionIndex];
            Transition.Reachable = true;
            Transition.NavDistance = NavDistance;
        }
    }

    OutGraph.PrecomputedNavQueryCount = NavQueryCount;

    const double BuildMilliseconds = (FPlatformTime::Seconds() - BuildStartSeconds) * 1000.0;
    UE_LOG(
        LogPathLinkRouteFinder,
        Log,
        TEXT("[PathLink][RouteCache] Build 완료 | Links=%d | Traversals=%d | LinkToLinkNavQueries=%d | Time=%.3fms | Context=%s"),
        Links.Num(),
        TraversalCount,
        NavQueryCount,
        BuildMilliseconds,
        *GetNameSafe(PathfindingContext));

    return true;
}

bool FPathLinkRouteFinder::FindShortestRoute(
    const FVector& StartLocation,
    const FVector& TargetLocation,
    const FPathLinkStaticGraph& StaticGraph,
    AActor* PathfindingContext,
    FPathLinkRouteResult& OutResult) const
{
    OutResult.Reset();

    if (!IsValid(World))
    {
        return false;
    }

    if (StartLocation.Equals(TargetLocation, 1.0f))
    {
        OutResult.Success = true;
        OutResult.TotalDistance = 0.0;
        return true;
    }

    const double RouteStartSeconds = FPlatformTime::Seconds();

    FVector StartNavLocation;
    FVector TargetNavLocation;
    if (!ProjectToNavigation(World, StartLocation, StartNavLocation)
        || !ProjectToNavigation(World, TargetLocation, TargetNavLocation))
    {
        return false;
    }

    const UPathLinkSettings* Settings = GetDefault<UPathLinkSettings>();
    const int32 InitialCandidateCount = FMath::Max(1, Settings ? Settings->DynamicCandidateCount : 6);
    const int32 ExpansionStep = FMath::Max(1, Settings ? Settings->CandidateExpansionStep : 3);
    const int32 MaxExpansionPasses = FMath::Max(0, Settings ? Settings->MaxCandidateExpansionPasses : 3);
    const double BlockedMultiplier = FMath::Max(
        1.0,
        static_cast<double>(Settings ? Settings->BlockedTraceDistanceMultiplier : 1.5f));
    const double ExcessiveRatio = FMath::Max(
        1.0,
        static_cast<double>(Settings ? Settings->ExcessiveNavDistanceRatio : 3.5f));
    const float TraceHeight = FMath::Max(
        0.0f,
        Settings ? Settings->CandidateTraceHeight : 100.0f);

    const int32 TraversalCount = StaticGraph.Traversals.Num();
    const int32 StartNode = 0;
    const int32 FirstTraversalNode = 1;
    const int32 TargetNode = FirstTraversalNode + TraversalCount;
    const int32 NodeCount = TargetNode + 1;

    struct FEndpointCandidate
    {
        TWeakObjectPtr<APathLink> Link;
        TArray<int32> TraversalIndices;

        /** Nearest N을 고를 때 사용하는 순수 직선거리입니다. */
        double BestStraightDistance = TNumericLimits<double>::Max();
    };

    struct FEndpointState
    {
        /** 현재 후보 Pool에 들어온 Traversal인지 여부입니다. */
        bool bActive = false;

        /** 실제 NavMesh 확인 결과 도달 불가능(Island 등)하여 제외된 Traversal입니다. */
        bool bRejected = false;

        /** 선택된 뒤 실제 NavMesh 거리까지 확인한 Traversal입니다. */
        bool bExactDistanceReady = false;

        /** 직선거리 + LineTrace 보정으로 얻은 싼 근사 거리입니다. */
        double EstimatedDistance = 0.0;

        /** 실제 NavMesh로 확인한 거리입니다. */
        double ExactNavDistance = 0.0;

        bool bTraceBlocked = false;
    };

    /**
     * 후보용 Trace는 지면에 바로 맞는 것을 줄이기 위해 시작/끝을 같은 Z Offset만큼 올립니다.
     * PathfindingContext, 해당 PathLink, 해당 Link의 ExitActor는 자기 자신을 장애물로 보지 않도록 제외합니다.
     */
    auto IsCandidateTraceBlocked = [this, PathfindingContext, TraceHeight](
        const FVector& RawFrom,
        const FVector& RawTo,
        const APathLink* Link) -> bool
    {
        if (RawFrom.Equals(RawTo, 1.0f))
        {
            return false;
        }

        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PathLinkDynamicCandidateTrace), false);

        if (IsValid(PathfindingContext))
        {
            QueryParams.AddIgnoredActor(PathfindingContext);
        }

        if (IsValid(Link))
        {
            QueryParams.AddIgnoredActor(Link);

            if (AActor* ExitActor = Link->GetExitActor(); IsValid(ExitActor))
            {
                QueryParams.AddIgnoredActor(ExitActor);
            }
        }

        FCollisionObjectQueryParams ObjectQueryParams;
        ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

        FHitResult Hit;
        const FVector HeightOffset = FVector::UpVector * TraceHeight;

        return World->LineTraceSingleByObjectType(
            Hit,
            RawFrom + HeightOffset,
            RawTo + HeightOffset,
            ObjectQueryParams,
            QueryParams);
    };

    /**
     * Nearest는 "Traversal 수"가 아니라 실제 PathLink Actor 수를 기준으로 셉니다.
     * TwoWay Link가 선택되면 그 Link의 Forward/Reverse Traversal은 둘 다 후보로 활성화합니다.
     *
     * 여기서는 아직 LineTrace/NavMesh Query를 하지 않습니다.
     * 모든 Link에 대해 직선거리만 계산하고 정렬하므로 비용이 매우 작습니다.
     */
    auto BuildEndpointCandidates = [&StaticGraph](
        const FVector& AnchorRawLocation,
        const bool bStartSide) -> TArray<FEndpointCandidate>
    {
        TArray<FEndpointCandidate> Candidates;
        TMap<APathLink*, int32> CandidateIndexByLink;

        for (int32 TraversalIndex = 0; TraversalIndex < StaticGraph.Traversals.Num(); ++TraversalIndex)
        {
            const FPathLinkTraversalNode& Traversal = StaticGraph.Traversals[TraversalIndex];
            APathLink* Link = Traversal.Link.Get();

            // ExitActor가 없는 PathLink는 StaticGraph에 원칙적으로 들어오지 않지만
            // 방어적으로 여기서도 제외합니다.
            if (!IsValid(Link) || !IsValid(Link->GetExitActor()))
            {
                continue;
            }

            const FVector EndpointLocation = bStartSide
                ? Traversal.EntryLocation
                : Traversal.ExitLocation;

            const double StraightDistance = FVector::Distance(
                AnchorRawLocation,
                EndpointLocation);

            int32* ExistingIndex = CandidateIndexByLink.Find(Link);
            if (!ExistingIndex)
            {
                FEndpointCandidate& Candidate = Candidates.AddDefaulted_GetRef();
                Candidate.Link = Link;
                Candidate.BestStraightDistance = StraightDistance;
                Candidate.TraversalIndices.Add(TraversalIndex);
                CandidateIndexByLink.Add(Link, Candidates.Num() - 1);
            }
            else
            {
                FEndpointCandidate& Candidate = Candidates[*ExistingIndex];
                Candidate.BestStraightDistance = FMath::Min(
                    Candidate.BestStraightDistance,
                    StraightDistance);
                Candidate.TraversalIndices.Add(TraversalIndex);
            }
        }

        Candidates.Sort([](const FEndpointCandidate& A, const FEndpointCandidate& B)
        {
            return A.BestStraightDistance < B.BestStraightDistance;
        });

        return Candidates;
    };

    const TArray<FEndpointCandidate> StartCandidates = BuildEndpointCandidates(
        StartLocation,
        true);

    const TArray<FEndpointCandidate> EndCandidates = BuildEndpointCandidates(
        TargetLocation,
        false);

    TArray<FEndpointState> StartStates;
    StartStates.SetNum(TraversalCount);

    TArray<FEndpointState> EndStates;
    EndStates.SetNum(TraversalCount);

    int32 StartCandidateCursor = 0;
    int32 EndCandidateCursor = 0;
    int32 StartActivatedLinkCount = 0;
    int32 EndActivatedLinkCount = 0;

    /**
     * 정렬된 Nearest 후보 중 다음 N개의 PathLink만 Pool에 추가합니다.
     * 이때에만 LineTrace를 수행하고, Hit이면 직선거리 * 보정배수로 EstimatedDistance를 만듭니다.
     * 실제 NavMesh Query는 아직 하지 않습니다.
     */
    auto ActivateNextCandidates = [
        &StaticGraph,
        &StartCandidates,
        &EndCandidates,
        &StartStates,
        &EndStates,
        &StartCandidateCursor,
        &EndCandidateCursor,
        &StartActivatedLinkCount,
        &EndActivatedLinkCount,
        StartLocation,
        TargetLocation,
        BlockedMultiplier,
        &IsCandidateTraceBlocked](
        const bool bStartSide,
        const int32 RequestedLinkCount)
    {
        const TArray<FEndpointCandidate>& Candidates = bStartSide
            ? StartCandidates
            : EndCandidates;

        TArray<FEndpointState>& States = bStartSide
            ? StartStates
            : EndStates;

        int32& Cursor = bStartSide
            ? StartCandidateCursor
            : EndCandidateCursor;

        int32& ActivatedLinkCount = bStartSide
            ? StartActivatedLinkCount
            : EndActivatedLinkCount;

        int32 AddedLinkCount = 0;

        while (Cursor < Candidates.Num() && AddedLinkCount < RequestedLinkCount)
        {
            const FEndpointCandidate& Candidate = Candidates[Cursor++];
            ++AddedLinkCount;
            ++ActivatedLinkCount;

            for (const int32 TraversalIndex : Candidate.TraversalIndices)
            {
                if (!StaticGraph.Traversals.IsValidIndex(TraversalIndex)
                    || !States.IsValidIndex(TraversalIndex))
                {
                    continue;
                }

                FEndpointState& State = States[TraversalIndex];
                if (State.bActive)
                {
                    continue;
                }

                const FPathLinkTraversalNode& Traversal = StaticGraph.Traversals[TraversalIndex];
                APathLink* Link = Traversal.Link.Get();

                if (!IsValid(Link) || !IsValid(Link->GetExitActor()))
                {
                    continue;
                }

                const FVector EndpointLocation = bStartSide
                    ? Traversal.EntryLocation
                    : Traversal.ExitLocation;

                const FVector AnchorLocation = bStartSide
                    ? StartLocation
                    : TargetLocation;

                double EstimatedDistance = FVector::Distance(
                    AnchorLocation,
                    EndpointLocation);

                const bool bBlocked = bStartSide
                    ? IsCandidateTraceBlocked(StartLocation, EndpointLocation, Link)
                    : IsCandidateTraceBlocked(EndpointLocation, TargetLocation, Link);

                if (bBlocked)
                {
                    EstimatedDistance *= BlockedMultiplier;
                }

                State.bActive = true;
                State.EstimatedDistance = EstimatedDistance;
                State.bTraceBlocked = bBlocked;
            }
        }
    };

    ActivateNextCandidates(true, InitialCandidateCount);
    ActivateNextCandidates(false, InitialCandidateCount);

    int32 DynamicNavQueryCount = 0;

    /**
     * 순수 NavMesh Start -> Target은 비교 기준으로 하나만 정확히 계산합니다.
     * 이 1회 Query 덕분에 Link 후보가 근사값이어도 "그냥 걸어가는 편이 더 낫다"는 경로는 안정적으로 남습니다.
     */
    double DirectDistance = 0.0;
    const bool bDirectReachable = BuildNavigationDistance(
        World,
        StartNavLocation,
        TargetNavLocation,
        PathfindingContext,
        DirectDistance,
        &DynamicNavQueryCount);

    TArray<FDynamicRouteEdge> SelectedEdges;
    TArray<int32> SelectedRouteEdgeIndices;

    /**
     * 현재 활성화된 Start/End 후보와 정밀한 Link -> Link Static Graph를 합쳐 Dijkstra를 수행합니다.
     * Start/End Edge는 아직 NavMesh를 전부 계산하지 않고 EstimatedDistance를 사용하며,
     * 실제로 선택된 Endpoint만 이후에 정확한 NavMesh 거리로 검증/교체합니다.
     */
    auto SolveWithCurrentCandidates = [
        &StaticGraph,
        TraversalCount,
        StartNode,
        FirstTraversalNode,
        TargetNode,
        NodeCount,
        bDirectReachable,
        DirectDistance,
        &StartStates,
        &EndStates](
        TArray<FDynamicRouteEdge>& OutEdges,
        TArray<int32>& OutRouteEdgeIndices) -> bool
    {
        OutEdges.Reset();
        OutRouteEdgeIndices.Reset();

        TArray<TArray<int32>> Adjacency;
        Adjacency.SetNum(NodeCount);

        auto AddEdge = [&OutEdges, &Adjacency](FDynamicRouteEdge&& Edge)
        {
            const int32 EdgeIndex = OutEdges.Add(MoveTemp(Edge));

            if (Adjacency.IsValidIndex(OutEdges[EdgeIndex].From))
            {
                Adjacency[OutEdges[EdgeIndex].From].Add(EdgeIndex);
            }
        };

        if (bDirectReachable)
        {
            FDynamicRouteEdge DirectEdge;
            DirectEdge.From = StartNode;
            DirectEdge.To = TargetNode;
            DirectEdge.Cost = DirectDistance;
            DirectEdge.NavDistance = DirectDistance;
            DirectEdge.Type = EDynamicRouteEdgeType::DirectToTarget;
            AddEdge(MoveTemp(DirectEdge));
        }

        for (int32 TraversalIndex = 0; TraversalIndex < TraversalCount; ++TraversalIndex)
        {
            if (!StaticGraph.Traversals.IsValidIndex(TraversalIndex))
            {
                continue;
            }

            const FPathLinkTraversalNode& Traversal = StaticGraph.Traversals[TraversalIndex];
            if (!Traversal.Link.IsValid())
            {
                continue;
            }

            const int32 TraversalNode = FirstTraversalNode + TraversalIndex;

            if (StartStates.IsValidIndex(TraversalIndex))
            {
                const FEndpointState& StartState = StartStates[TraversalIndex];

                if (StartState.bActive && !StartState.bRejected)
                {
                    const double StartDistance = StartState.bExactDistanceReady
                        ? StartState.ExactNavDistance
                        : StartState.EstimatedDistance;

                    FDynamicRouteEdge StartEdge;
                    StartEdge.From = StartNode;
                    StartEdge.To = TraversalNode;
                    StartEdge.NavDistance = StartDistance;
                    StartEdge.Cost = StartDistance + Traversal.LinkCost;
                    StartEdge.Type = EDynamicRouteEdgeType::EnterTraversal;
                    StartEdge.TraversalIndex = TraversalIndex;
                    AddEdge(MoveTemp(StartEdge));
                }
            }

            if (EndStates.IsValidIndex(TraversalIndex))
            {
                const FEndpointState& EndState = EndStates[TraversalIndex];

                if (EndState.bActive && !EndState.bRejected)
                {
                    const double EndDistance = EndState.bExactDistanceReady
                        ? EndState.ExactNavDistance
                        : EndState.EstimatedDistance;

                    FDynamicRouteEdge TargetEdge;
                    TargetEdge.From = TraversalNode;
                    TargetEdge.To = TargetNode;
                    TargetEdge.NavDistance = EndDistance;
                    TargetEdge.Cost = EndDistance;
                    TargetEdge.Type = EDynamicRouteEdgeType::TraversalToTarget;
                    AddEdge(MoveTemp(TargetEdge));
                }
            }
        }

        // Link -> Link는 기존 정밀 NavMesh Cache를 그대로 사용합니다.
        for (int32 FromIndex = 0; FromIndex < TraversalCount; ++FromIndex)
        {
            const int32 FromNode = FirstTraversalNode + FromIndex;

            for (int32 ToIndex = 0; ToIndex < TraversalCount; ++ToIndex)
            {
                const FPathLinkCachedTransition* Transition = StaticGraph.GetTransition(
                    FromIndex,
                    ToIndex);

                if (!Transition || !Transition->Reachable)
                {
                    continue;
                }

                const FPathLinkTraversalNode& ToTraversal = StaticGraph.Traversals[ToIndex];
                if (!ToTraversal.Link.IsValid())
                {
                    continue;
                }

                FDynamicRouteEdge CachedEdge;
                CachedEdge.From = FromNode;
                CachedEdge.To = FirstTraversalNode + ToIndex;
                CachedEdge.NavDistance = Transition->NavDistance;
                CachedEdge.Cost = Transition->NavDistance + ToTraversal.LinkCost;
                CachedEdge.Type = EDynamicRouteEdgeType::EnterTraversal;
                CachedEdge.TraversalIndex = ToIndex;
                AddEdge(MoveTemp(CachedEdge));
            }
        }

        const double Infinity = TNumericLimits<double>::Max();

        TArray<double> Distances;
        Distances.Init(Infinity, NodeCount);

        TArray<int32> PreviousEdge;
        PreviousEdge.Init(INDEX_NONE, NodeCount);

        TArray<bool> Visited;
        Visited.Init(false, NodeCount);

        Distances[StartNode] = 0.0;

        // NavMesh Query가 아닌 Dijkstra 자체는 상대적으로 매우 저렴하므로 단순 구현을 유지합니다.
        for (int32 Iteration = 0; Iteration < NodeCount; ++Iteration)
        {
            int32 CurrentNode = INDEX_NONE;
            double CurrentDistance = Infinity;

            for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
            {
                if (!Visited[NodeIndex] && Distances[NodeIndex] < CurrentDistance)
                {
                    CurrentNode = NodeIndex;
                    CurrentDistance = Distances[NodeIndex];
                }
            }

            if (CurrentNode == INDEX_NONE)
            {
                break;
            }

            if (CurrentNode == TargetNode)
            {
                break;
            }

            Visited[CurrentNode] = true;

            for (const int32 EdgeIndex : Adjacency[CurrentNode])
            {
                if (!OutEdges.IsValidIndex(EdgeIndex))
                {
                    continue;
                }

                const FDynamicRouteEdge& Edge = OutEdges[EdgeIndex];
                const double NewDistance = Distances[CurrentNode] + Edge.Cost;

                if (NewDistance + KINDA_SMALL_NUMBER < Distances[Edge.To])
                {
                    Distances[Edge.To] = NewDistance;
                    PreviousEdge[Edge.To] = EdgeIndex;
                }
            }
        }

        if (PreviousEdge[TargetNode] == INDEX_NONE)
        {
            return false;
        }

        int32 TraceNode = TargetNode;

        while (TraceNode != StartNode)
        {
            const int32 EdgeIndex = PreviousEdge[TraceNode];

            if (!OutEdges.IsValidIndex(EdgeIndex))
            {
                OutRouteEdgeIndices.Reset();
                return false;
            }

            OutRouteEdgeIndices.Add(EdgeIndex);
            TraceNode = OutEdges[EdgeIndex].From;
        }

        Algo::Reverse(OutRouteEdgeIndices);
        return true;
    };

    /**
     * Dijkstra가 실제로 선택한 Start/End Endpoint만 정확한 NavMesh로 확인합니다.
     *
     * - Path가 없으면 Island/분리된 NavMesh로 판단하고 해당 Traversal을 후보에서 제외합니다.
     * - 실제 NavDistance가 추정치보다 ExcessiveRatio 이상 크면, 비용을 정확한 값으로 교체하고
     *   다음 후보를 조금 더 열어 다시 비교합니다.
     */
    auto ValidateSelectedEndpoint = [
        this,
        &StaticGraph,
        PathfindingContext,
        &DynamicNavQueryCount,
        StartNavLocation,
        TargetNavLocation,
        ExcessiveRatio,
        &StartStates,
        &EndStates](
        const bool bStartSide,
        const int32 TraversalIndex,
        bool& bOutRejected,
        bool& bOutExactDistanceUpdated,
        bool& bOutExcessive) -> bool
    {
        bOutRejected = false;
        bOutExactDistanceUpdated = false;
        bOutExcessive = false;

        if (!StaticGraph.Traversals.IsValidIndex(TraversalIndex))
        {
            return false;
        }

        TArray<FEndpointState>& States = bStartSide
            ? StartStates
            : EndStates;

        if (!States.IsValidIndex(TraversalIndex))
        {
            return false;
        }

        FEndpointState& State = States[TraversalIndex];

        if (!State.bActive || State.bRejected)
        {
            return false;
        }

        if (State.bExactDistanceReady)
        {
            return true;
        }

        const FPathLinkTraversalNode& Traversal = StaticGraph.Traversals[TraversalIndex];

        double ActualNavDistance = 0.0;
        const bool bReachable = bStartSide
            ? BuildNavigationDistance(
                World,
                StartNavLocation,
                Traversal.EntryNavLocation,
                PathfindingContext,
                ActualNavDistance,
                &DynamicNavQueryCount)
            : BuildNavigationDistance(
                World,
                Traversal.ExitNavLocation,
                TargetNavLocation,
                PathfindingContext,
                ActualNavDistance,
                &DynamicNavQueryCount);

        if (!bReachable)
        {
            // Nearest 후보가 다른 NavMesh Island에 있거나 실제로 도달 불가능하면 이 Traversal은 완전히 제외합니다.
            State.bRejected = true;
            bOutRejected = true;
            return false;
        }

        State.bExactDistanceReady = true;
        State.ExactNavDistance = ActualNavDistance;
        bOutExactDistanceUpdated = true;

        const double SafeEstimate = FMath::Max(1.0, State.EstimatedDistance);
        bOutExcessive = ActualNavDistance > SafeEstimate * ExcessiveRatio;

        return true;
    };

    int32 ExpansionPassesUsed = 0;

    // Endpoint 검증/비용 교체 때문에 같은 후보 Pool에서 몇 차례 재계산할 수 있습니다.
    // 각 Traversal의 Exact/Rejected 상태는 한 번만 바뀌므로 실제 반복 횟수는 제한적입니다.
    const int32 SafetyIterationLimit = FMath::Max(16, TraversalCount * 4 + 16);
    int32 SafetyIteration = 0;

    while (SafetyIteration++ < SafetyIterationLimit)
    {
        const bool bFoundRoute = SolveWithCurrentCandidates(
            SelectedEdges,
            SelectedRouteEdgeIndices);

        if (!bFoundRoute)
        {
            const bool bCanExpandStart = StartCandidateCursor < StartCandidates.Num();
            const bool bCanExpandEnd = EndCandidateCursor < EndCandidates.Num();

            if (ExpansionPassesUsed >= MaxExpansionPasses
                || (!bCanExpandStart && !bCanExpandEnd))
            {
                return false;
            }

            if (bCanExpandStart)
            {
                ActivateNextCandidates(true, ExpansionStep);
            }

            if (bCanExpandEnd)
            {
                ActivateNextCandidates(false, ExpansionStep);
            }

            ++ExpansionPassesUsed;
            continue;
        }

        int32 SelectedStartTraversal = INDEX_NONE;
        int32 SelectedEndTraversal = INDEX_NONE;

        for (const int32 EdgeIndex : SelectedRouteEdgeIndices)
        {
            if (!SelectedEdges.IsValidIndex(EdgeIndex))
            {
                continue;
            }

            const FDynamicRouteEdge& Edge = SelectedEdges[EdgeIndex];

            if (Edge.Type == EDynamicRouteEdgeType::EnterTraversal
                && Edge.From == StartNode)
            {
                SelectedStartTraversal = Edge.TraversalIndex;
            }
            else if (Edge.Type == EDynamicRouteEdgeType::TraversalToTarget)
            {
                SelectedEndTraversal = Edge.From - FirstTraversalNode;
            }
        }

        // Direct NavMesh Route라면 Link Endpoint 검증 자체가 필요 없습니다.
        if (SelectedStartTraversal == INDEX_NONE
            && SelectedEndTraversal == INDEX_NONE)
        {
            break;
        }

        bool bNeedResolveAgain = false;
        bool bNeedExpandStart = false;
        bool bNeedExpandEnd = false;

        if (SelectedStartTraversal != INDEX_NONE)
        {
            bool bRejected = false;
            bool bExactUpdated = false;
            bool bExcessive = false;

            ValidateSelectedEndpoint(
                true,
                SelectedStartTraversal,
                bRejected,
                bExactUpdated,
                bExcessive);

            if (bRejected || bExactUpdated)
            {
                bNeedResolveAgain = true;
            }

            // Island이면 다음 후보를 보충하고,
            // 실제 NavPath가 추정치보다 지나치게 길어도 다음 후보를 조금 더 열어 비교합니다.
            bNeedExpandStart = bRejected || bExcessive;
        }

        if (SelectedEndTraversal != INDEX_NONE)
        {
            bool bRejected = false;
            bool bExactUpdated = false;
            bool bExcessive = false;

            ValidateSelectedEndpoint(
                false,
                SelectedEndTraversal,
                bRejected,
                bExactUpdated,
                bExcessive);

            if (bRejected || bExactUpdated)
            {
                bNeedResolveAgain = true;
            }

            bNeedExpandEnd = bRejected || bExcessive;
        }

        const bool bCanExpandStart = bNeedExpandStart
            && StartCandidateCursor < StartCandidates.Num();
        const bool bCanExpandEnd = bNeedExpandEnd
            && EndCandidateCursor < EndCandidates.Num();

        if (ExpansionPassesUsed < MaxExpansionPasses
            && (bCanExpandStart || bCanExpandEnd))
        {
            if (bCanExpandStart)
            {
                ActivateNextCandidates(
                    true,
                    bNeedExpandStart ? ExpansionStep : 0);
            }

            if (bCanExpandEnd)
            {
                ActivateNextCandidates(
                    false,
                    bNeedExpandEnd ? ExpansionStep : 0);
            }

            ++ExpansionPassesUsed;
            bNeedResolveAgain = true;
        }

        if (bNeedResolveAgain)
        {
            continue;
        }

        // 선택된 Start/End Edge가 이미 Exact 상태이고 더 확장할 이유도 없으면 최종 Route로 확정합니다.
        break;
    }

    if (SafetyIteration > SafetyIterationLimit)
    {
        UE_LOG(
            LogPathLinkRouteFinder,
            Warning,
            TEXT("[PathLink][Route] 후보 검증 반복 상한에 도달했습니다. Traversals=%d | Context=%s"),
            TraversalCount,
            *GetNameSafe(PathfindingContext));
        return false;
    }

    auto AddNormalSegment = [this, PathfindingContext, &OutResult, &DynamicNavQueryCount](
        const FVector& RawStart,
        const FVector& NavStart,
        const FVector& RawEnd,
        const FVector& NavEnd) -> bool
    {
        double ActualDistance = 0.0;
        TArray<FVector> PathPoints;

        if (!BuildNavigationPath(
            World,
            NavStart,
            NavEnd,
            PathfindingContext,
            ActualDistance,
            PathPoints,
            &DynamicNavQueryCount))
        {
            return false;
        }

        FPathLinkRouteSegment& Segment = OutResult.Segments.AddDefaulted_GetRef();
        Segment.SegmentType = EPathLinkSegmentType::Normal;
        Segment.StartLocation = RawStart;
        Segment.EndLocation = RawEnd;
        Segment.Distance = ActualDistance;
        Segment.PathPoints = MoveTemp(PathPoints);

        OutResult.TotalDistance += ActualDistance;
        return true;
    };

    auto AddLinkSegment = [&OutResult](const FPathLinkTraversalNode& Traversal)
    {
        FPathLinkRouteSegment& Segment = OutResult.Segments.AddDefaulted_GetRef();
        Segment.SegmentType = EPathLinkSegmentType::Link;
        Segment.StartLocation = Traversal.EntryLocation;
        Segment.EndLocation = Traversal.ExitLocation;
        Segment.Distance = Traversal.LinkCost;
        Segment.Link = Traversal.Link.Get();
        Segment.Reverse = Traversal.Reverse;
        Segment.LinkType = Traversal.LinkType;

        OutResult.TotalDistance += Traversal.LinkCost;

        if (APathLink* UsedLink = Traversal.Link.Get(); IsValid(UsedLink))
        {
            OutResult.UsedLinks.Add(UsedLink);
        }
    };

    OutResult.TotalDistance = 0.0;

    for (const int32 EdgeIndex : SelectedRouteEdgeIndices)
    {
        if (!SelectedEdges.IsValidIndex(EdgeIndex))
        {
            OutResult.Reset();
            return false;
        }

        const FDynamicRouteEdge& Edge = SelectedEdges[EdgeIndex];

        if (Edge.Type == EDynamicRouteEdgeType::DirectToTarget)
        {
            if (!AddNormalSegment(
                StartLocation,
                StartNavLocation,
                TargetLocation,
                TargetNavLocation))
            {
                OutResult.Reset();
                return false;
            }

            continue;
        }

        if (Edge.Type == EDynamicRouteEdgeType::TraversalToTarget)
        {
            const int32 SourceTraversalIndex = Edge.From - FirstTraversalNode;

            if (!StaticGraph.Traversals.IsValidIndex(SourceTraversalIndex))
            {
                OutResult.Reset();
                return false;
            }

            const FPathLinkTraversalNode& SourceTraversal = StaticGraph.Traversals[SourceTraversalIndex];

            if (!AddNormalSegment(
                SourceTraversal.ExitLocation,
                SourceTraversal.ExitNavLocation,
                TargetLocation,
                TargetNavLocation))
            {
                OutResult.Reset();
                return false;
            }

            continue;
        }

        if (!StaticGraph.Traversals.IsValidIndex(Edge.TraversalIndex))
        {
            OutResult.Reset();
            return false;
        }

        const FPathLinkTraversalNode& TargetTraversal = StaticGraph.Traversals[Edge.TraversalIndex];

        FVector RawNormalStart = StartLocation;
        FVector NavNormalStart = StartNavLocation;

        if (Edge.From != StartNode)
        {
            const int32 SourceTraversalIndex = Edge.From - FirstTraversalNode;

            if (!StaticGraph.Traversals.IsValidIndex(SourceTraversalIndex))
            {
                OutResult.Reset();
                return false;
            }

            const FPathLinkTraversalNode& SourceTraversal = StaticGraph.Traversals[SourceTraversalIndex];
            RawNormalStart = SourceTraversal.ExitLocation;
            NavNormalStart = SourceTraversal.ExitNavLocation;
        }

        if (!AddNormalSegment(
            RawNormalStart,
            NavNormalStart,
            TargetTraversal.EntryLocation,
            TargetTraversal.EntryNavLocation))
        {
            OutResult.Reset();
            return false;
        }

        AddLinkSegment(TargetTraversal);
    }

    OutResult.Success = true;

    int32 StartTraceBlockedCount = 0;
    int32 EndTraceBlockedCount = 0;
    int32 RejectedStartTraversalCount = 0;
    int32 RejectedEndTraversalCount = 0;

    for (const FEndpointState& State : StartStates)
    {
        StartTraceBlockedCount += (State.bActive && State.bTraceBlocked) ? 1 : 0;
        RejectedStartTraversalCount += State.bRejected ? 1 : 0;
    }

    for (const FEndpointState& State : EndStates)
    {
        EndTraceBlockedCount += (State.bActive && State.bTraceBlocked) ? 1 : 0;
        RejectedEndTraversalCount += State.bRejected ? 1 : 0;
    }

    const double RouteMilliseconds = (FPlatformTime::Seconds() - RouteStartSeconds) * 1000.0;

    UE_LOG(
        LogPathLinkRouteFinder,
        Verbose,
        TEXT("[PathLink][Route] Success | Traversals=%d | CandidateLinks(Start=%d/%d End=%d/%d) | TraceBlocked(Start=%d End=%d) | RejectedIslandTraversal(Start=%d End=%d) | ExpansionPasses=%d | UsedLinks=%d | DynamicNavQueries=%d | Segments=%d | Distance=%.2f | Time=%.3fms | Context=%s"),
        TraversalCount,
        StartActivatedLinkCount,
        StartCandidates.Num(),
        EndActivatedLinkCount,
        EndCandidates.Num(),
        StartTraceBlockedCount,
        EndTraceBlockedCount,
        RejectedStartTraversalCount,
        RejectedEndTraversalCount,
        ExpansionPassesUsed,
        OutResult.UsedLinks.Num(),
        DynamicNavQueryCount,
        OutResult.Segments.Num(),
        OutResult.TotalDistance,
        RouteMilliseconds,
        *GetNameSafe(PathfindingContext));

    return true;
}

