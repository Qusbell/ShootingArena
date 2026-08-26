#include "AI/PathLink/Internal/PathLinkRouteFinder.h"

#include "AI/PathLink/PathLink.h"
#include "Algo/Reverse.h"
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

    const int32 TraversalCount = StaticGraph.Traversals.Num();
    const int32 StartNode = 0;
    const int32 FirstTraversalNode = 1;
    const int32 TargetNode = FirstTraversalNode + TraversalCount;
    const int32 NodeCount = TargetNode + 1;

    TArray<FDynamicRouteEdge> Edges;
    Edges.Reserve(1 + TraversalCount * 2 + TraversalCount * TraversalCount);

    TArray<TArray<int32>> Adjacency;
    Adjacency.SetNum(NodeCount);

    auto AddEdge = [&Edges, &Adjacency](FDynamicRouteEdge&& Edge)
    {
        const int32 EdgeIndex = Edges.Add(MoveTemp(Edge));
        if (Adjacency.IsValidIndex(Edges[EdgeIndex].From))
        {
            Adjacency[Edges[EdgeIndex].From].Add(EdgeIndex);
        }
    };

    int32 DynamicNavQueryCount = 0;

    // Start -> Target 직접 NavMesh 경로도 항상 후보에 포함합니다.
    double DirectDistance = 0.0;
    if (BuildNavigationDistance(
        World,
        StartNavLocation,
        TargetNavLocation,
        PathfindingContext,
        DirectDistance,
        &DynamicNavQueryCount))
    {
        FDynamicRouteEdge DirectEdge;
        DirectEdge.From = StartNode;
        DirectEdge.To = TargetNode;
        DirectEdge.Cost = DirectDistance;
        DirectEdge.NavDistance = DirectDistance;
        DirectEdge.Type = EDynamicRouteEdgeType::DirectToTarget;
        AddEdge(MoveTemp(DirectEdge));
    }

    // Start/Target은 요청마다 달라지므로 이 두 종류만 동적으로 NavMesh 조회합니다.
    for (int32 TraversalIndex = 0; TraversalIndex < TraversalCount; ++TraversalIndex)
    {
        const FPathLinkTraversalNode& Traversal = StaticGraph.Traversals[TraversalIndex];
        if (!Traversal.Link.IsValid())
        {
            continue;
        }

        const int32 TraversalNode = FirstTraversalNode + TraversalIndex;

        double StartToEntryDistance = 0.0;
        if (BuildNavigationDistance(
            World,
            StartNavLocation,
            Traversal.EntryNavLocation,
            PathfindingContext,
            StartToEntryDistance,
            &DynamicNavQueryCount))
        {
            FDynamicRouteEdge StartEdge;
            StartEdge.From = StartNode;
            StartEdge.To = TraversalNode;
            StartEdge.NavDistance = StartToEntryDistance;
            StartEdge.Cost = StartToEntryDistance + Traversal.LinkCost;
            StartEdge.Type = EDynamicRouteEdgeType::EnterTraversal;
            StartEdge.TraversalIndex = TraversalIndex;
            AddEdge(MoveTemp(StartEdge));
        }

        double ExitToTargetDistance = 0.0;
        if (BuildNavigationDistance(
            World,
            Traversal.ExitNavLocation,
            TargetNavLocation,
            PathfindingContext,
            ExitToTargetDistance,
            &DynamicNavQueryCount))
        {
            FDynamicRouteEdge TargetEdge;
            TargetEdge.From = TraversalNode;
            TargetEdge.To = TargetNode;
            TargetEdge.NavDistance = ExitToTargetDistance;
            TargetEdge.Cost = ExitToTargetDistance;
            TargetEdge.Type = EDynamicRouteEdgeType::TraversalToTarget;
            AddEdge(MoveTemp(TargetEdge));
        }
    }

    // Link -> Link는 미리 계산한 거리 Cache를 Graph Edge로 재사용합니다.
    for (int32 FromIndex = 0; FromIndex < TraversalCount; ++FromIndex)
    {
        const int32 FromNode = FirstTraversalNode + FromIndex;

        for (int32 ToIndex = 0; ToIndex < TraversalCount; ++ToIndex)
        {
            const FPathLinkCachedTransition* Transition = StaticGraph.GetTransition(FromIndex, ToIndex);
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

    // NavMesh Query가 아닌 이 Dijkstra 자체는 상대적으로 매우 저렴하므로 단순 구현을 유지합니다.
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
            if (!Edges.IsValidIndex(EdgeIndex))
            {
                continue;
            }

            const FDynamicRouteEdge& Edge = Edges[EdgeIndex];
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

    TArray<int32> RouteEdgeIndices;
    int32 TraceNode = TargetNode;

    while (TraceNode != StartNode)
    {
        const int32 EdgeIndex = PreviousEdge[TraceNode];
        if (!Edges.IsValidIndex(EdgeIndex))
        {
            OutResult.Reset();
            return false;
        }

        RouteEdgeIndices.Add(EdgeIndex);
        TraceNode = Edges[EdgeIndex].From;
    }

    Algo::Reverse(RouteEdgeIndices);

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

    for (const int32 EdgeIndex : RouteEdgeIndices)
    {
        const FDynamicRouteEdge& Edge = Edges[EdgeIndex];

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

    const double RouteMilliseconds = (FPlatformTime::Seconds() - RouteStartSeconds) * 1000.0;
    UE_LOG(
        LogPathLinkRouteFinder,
        Verbose,
        TEXT("[PathLink][Route] Success | Traversals=%d | UsedLinks=%d | DynamicNavQueries=%d | Segments=%d | Distance=%.2f | Time=%.3fms | Context=%s"),
        TraversalCount,
        OutResult.UsedLinks.Num(),
        DynamicNavQueryCount,
        OutResult.Segments.Num(),
        OutResult.TotalDistance,
        RouteMilliseconds,
        *GetNameSafe(PathfindingContext));

    return true;
}
