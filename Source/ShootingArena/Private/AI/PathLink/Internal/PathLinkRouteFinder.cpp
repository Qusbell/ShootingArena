#include "AI/PathLink/Internal/PathLinkRouteFinder.h"

#include "AI/PathLink/PathLink.h"
#include "Algo/Reverse.h"
#include "Engine/World.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace
{
    const FVector NavProjectionExtent(150.0, 150.0, 400.0);

    struct FGraphNode
    {
        /** BP/기믹이 실제로 사용하는 위치입니다. */
        FVector RawLocation = FVector::ZeroVector;

        /** NavMesh 경로 계산용으로 보정된 위치입니다. */
        FVector NavLocation = FVector::ZeroVector;
    };

    struct FGraphEdge
    {
        int32 From = INDEX_NONE;
        int32 To = INDEX_NONE;
        double Cost = 0.0;
        EPathLinkSegmentType SegmentType = EPathLinkSegmentType::Normal;

        TWeakObjectPtr<APathLink> Link;
        bool Reverse = false;
        EPathLinkType LinkType = EPathLinkType::Teleport;

        /** Normal Edge일 때만 NavMesh Path Point를 저장합니다. */
        TArray<FVector> PathPoints;

        /** Link Edge일 때 실제 기믹 진입/출구 위치를 보존합니다. */
        FVector LinkEntryLocation = FVector::ZeroVector;
        FVector LinkExitLocation = FVector::ZeroVector;
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

    bool BuildNavigationEdge(
        UWorld* World,
        const FGraphNode& FromNode,
        const FGraphNode& ToNode,
        AActor* PathfindingContext,
        double& OutDistance,
        TArray<FVector>& OutPathPoints)
    {
        OutDistance = 0.0;
        OutPathPoints.Reset();

        if (FromNode.NavLocation.Equals(ToNode.NavLocation, 1.0f))
        {
            OutPathPoints.Add(FromNode.NavLocation);
            OutPathPoints.Add(ToNode.NavLocation);
            return true;
        }

        UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
            World,
            FromNode.NavLocation,
            ToNode.NavLocation,
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
}

bool FPathLinkRouteFinder::FindShortestRoute(
    const FVector& StartLocation,
    const FVector& TargetLocation,
    const TArray<APathLink*>& Links,
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

    // Node 0 = Start, Node 1 = Target으로 고정합니다.
    TArray<FGraphNode> Nodes;
    Nodes.Reserve(2 + Links.Num() * 4);

    FVector StartNavLocation;
    FVector TargetNavLocation;

    if (!ProjectToNavigation(World, StartLocation, StartNavLocation)
        || !ProjectToNavigation(World, TargetLocation, TargetNavLocation))
    {
        return false;
    }

    Nodes.Add({ StartLocation, StartNavLocation });
    Nodes.Add({ TargetLocation, TargetNavLocation });

    TArray<FGraphEdge> LinkEdges;
    LinkEdges.Reserve(Links.Num() * 2);

    auto AddLinkDirection = [&Nodes, &LinkEdges](APathLink* Link, const bool Reverse)
    {
        FVector EntryLocation;
        FVector ExitLocation;
        FString FailureReason;

        if (!IsValid(Link)
            || !Link->TryResolveTravelLocations(Reverse, EntryLocation, ExitLocation, FailureReason))
        {
            return;
        }

        UWorld* LinkWorld = Link->GetWorld();
        if (!IsValid(LinkWorld))
        {
            return;
        }

        FVector EntryNavLocation;
        FVector ExitNavLocation;
        if (!ProjectToNavigation(LinkWorld, EntryLocation, EntryNavLocation)
            || !ProjectToNavigation(LinkWorld, ExitLocation, ExitNavLocation))
        {
            return;
        }

        const int32 EntryNode = Nodes.Add({ EntryLocation, EntryNavLocation });
        const int32 ExitNode = Nodes.Add({ ExitLocation, ExitNavLocation });

        FGraphEdge& Edge = LinkEdges.AddDefaulted_GetRef();
        Edge.From = EntryNode;
        Edge.To = ExitNode;
        Edge.Cost = Link->GetTravelDistance(Reverse);
        Edge.SegmentType = EPathLinkSegmentType::Link;
        Edge.Link = Link;
        Edge.Reverse = Reverse;
        Edge.LinkType = Link->GetLinkType();
        Edge.LinkEntryLocation = EntryLocation;
        Edge.LinkExitLocation = ExitLocation;
    };

    for (APathLink* Link : Links)
    {
        if (!IsValid(Link) || !Link->IsUsable())
        {
            continue;
        }

        AddLinkDirection(Link, false);

        if (Link->IsTwoWay())
        {
            // 역방향은 단순 좌표 Swap이 아니라,
            // Teleport의 PortalTrigger / ExitDirection처럼 해당 방향에 맞춰 다시 Resolve합니다.
            AddLinkDirection(Link, true);
        }
    }

    TArray<FGraphEdge> Edges;
    TArray<TArray<int32>> Adjacency;
    Adjacency.SetNum(Nodes.Num());

    auto AddEdge = [&Edges, &Adjacency](FGraphEdge&& Edge)
    {
        const int32 EdgeIndex = Edges.Add(MoveTemp(Edge));
        if (Adjacency.IsValidIndex(Edges[EdgeIndex].From))
        {
            Adjacency[Edges[EdgeIndex].From].Add(EdgeIndex);
        }
    };

    // 일반 이동은 모든 Node 쌍을 NavMesh로 실제 조회합니다.
    // 따라서 벽/장애물 때문에 직선거리가 짧아도 실제 NavMesh 우회거리를 Cost로 사용합니다.
    for (int32 From = 0; From < Nodes.Num(); ++From)
    {
        for (int32 To = 0; To < Nodes.Num(); ++To)
        {
            if (From == To)
            {
                continue;
            }

            double NavDistance = 0.0;
            TArray<FVector> PathPoints;
            if (!BuildNavigationEdge(
                World,
                Nodes[From],
                Nodes[To],
                PathfindingContext,
                NavDistance,
                PathPoints))
            {
                continue;
            }

            FGraphEdge Edge;
            Edge.From = From;
            Edge.To = To;
            Edge.Cost = NavDistance;
            Edge.SegmentType = EPathLinkSegmentType::Normal;
            Edge.PathPoints = MoveTemp(PathPoints);
            AddEdge(MoveTemp(Edge));
        }
    }

    // 특수 이동 Edge를 마지막에 추가합니다.
    for (FGraphEdge& LinkEdge : LinkEdges)
    {
        AddEdge(MoveTemp(LinkEdge));
    }

    if (Nodes.Num() < 2)
    {
        return false;
    }

    const int32 StartNode = 0;
    const int32 TargetNode = 1;
    const double Infinity = TNumericLimits<double>::Max();

    TArray<double> Distances;
    Distances.Init(Infinity, Nodes.Num());

    TArray<int32> PreviousEdge;
    PreviousEdge.Init(INDEX_NONE, Nodes.Num());

    TArray<bool> Visited;
    Visited.Init(false, Nodes.Num());

    Distances[StartNode] = 0.0;

    // Link 수가 보통 많지 않은 레벨 배치용 시스템이므로 단순하고 안정적인 Dijkstra를 사용합니다.
    for (int32 Iteration = 0; Iteration < Nodes.Num(); ++Iteration)
    {
        int32 CurrentNode = INDEX_NONE;
        double CurrentDistance = Infinity;

        for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
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

            const FGraphEdge& Edge = Edges[EdgeIndex];
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

    OutResult.Success = true;
    OutResult.TotalDistance = Distances[TargetNode];

    for (const int32 EdgeIndex : RouteEdgeIndices)
    {
        const FGraphEdge& Edge = Edges[EdgeIndex];

        FPathLinkRouteSegment& Segment = OutResult.Segments.AddDefaulted_GetRef();
        Segment.SegmentType = Edge.SegmentType;
        Segment.Distance = Edge.Cost;

        if (Edge.SegmentType == EPathLinkSegmentType::Normal)
        {
            Segment.StartLocation = Nodes[Edge.From].RawLocation;
            Segment.EndLocation = Nodes[Edge.To].RawLocation;
            Segment.PathPoints = Edge.PathPoints;
        }
        else
        {
            Segment.StartLocation = Edge.LinkEntryLocation;
            Segment.EndLocation = Edge.LinkExitLocation;
            Segment.Link = Edge.Link.Get();
            Segment.Reverse = Edge.Reverse;
            Segment.LinkType = Edge.LinkType;

            if (APathLink* UsedLink = Edge.Link.Get(); IsValid(UsedLink))
            {
                OutResult.UsedLinks.Add(UsedLink);
            }
        }
    }

    return true;
}
