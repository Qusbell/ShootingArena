#include "AI/PathLink/PathLinkSubsystem.h"

#include "AI/PathLink/PathLink.h"
#include "AI/PathLink/Internal/PathLinkRouteFinder.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "DrawDebugHelpers.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Misc/MessageDialog.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogPathLinkSubsystem, Log, All);

void UPathLinkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    RegisteredLinks.Reset();
    InvalidateRouteGraphCache();
}

void UPathLinkSubsystem::Deinitialize()
{
    RegisteredLinks.Reset();
    CachedRouteLinks.Reset();
    DefaultRouteGraph.Reset();
    ContextRouteGraphs.Reset();
    bRouteGraphCacheDirty = true;
    bCommonRouteCacheReady = false;
    bDefaultRouteGraphReady = false;
    bCachedHasBlockingDuplicates = false;
    CachedDuplicateSummary.Reset();
    Super::Deinitialize();
}

void UPathLinkSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    // BeginPlay 호출 순서에 상관없이 현재 레벨의 Link를 한 번 확실하게 수집합니다.
    // 이후 Streaming으로 새로 들어오는 Link는 각 APathLink::BeginPlay에서 자동 등록됩니다.
    RefreshLinks();

    // 구조 Validation / Duplicate Scan은 여기서 Common Cache에 한 번 저장합니다.
    // 반복 Route 요청에서는 같은 Ground Trace / Duplicate Scan을 다시 수행하지 않습니다.
    RebuildCommonRouteCache();

    if (bCachedHasBlockingDuplicates)
    {
        UE_LOG(
            LogPathLinkSubsystem,
            Error,
            TEXT("[PathLink][BLOCKED][DuplicatePlacement] 중복 PathLink가 발견되어 PathLink 시스템 실행을 차단합니다.\n%s"),
            *CachedDuplicateSummary);

#if WITH_EDITOR
        if (InWorld.WorldType == EWorldType::PIE)
        {
            BlockEditorPlayForDuplicates(CachedDuplicateSummary);
        }
#endif
        return;
    }

    // 기본 Context(nullptr)는 World 시작 시 Link -> Link Graph를 미리 계산합니다.
    // NavMesh가 아직 준비되지 않았다면 실패 상태를 캐시하지 않고 첫 Route 요청에서 다시 시도합니다.
    if (InWorld.GetNetMode() != NM_Client)
    {
        const FPathLinkStaticGraph* PrewarmedGraph = nullptr;
        if (!EnsureRouteGraphCache(nullptr, PrewarmedGraph))
        {
            UE_LOG(
                LogPathLinkSubsystem,
                Warning,
                TEXT("[PathLink][RouteCache] World BeginPlay 사전 구축을 완료하지 못했습니다. 첫 Route 요청에서 다시 시도합니다."));
        }
    }
}

void UPathLinkSubsystem::RefreshLinks()
{
    RegisteredLinks.Reset();
    InvalidateRouteGraphCache();

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    for (TActorIterator<APathLink> It(World); It; ++It)
    {
        RegisterLink(*It);
    }

    int32 ValidCount = 0;
    int32 InvalidCount = 0;
    int32 EnabledCount = 0;
    int32 DisabledCount = 0;

    for (const TWeakObjectPtr<APathLink>& WeakLink : RegisteredLinks)
    {
        APathLink* Link = WeakLink.Get();
        if (!IsValid(Link))
        {
            continue;
        }

        if (Link->IsValidLink())
        {
            ++ValidCount;
            if (Link->IsEnabled())
            {
                ++EnabledCount;
            }
            else
            {
                ++DisabledCount;
            }
        }
        else
        {
            ++InvalidCount;
        }
    }

    UE_LOG(
        LogPathLinkSubsystem,
        Log,
        TEXT("[PathLink][Registry] Refresh 완료 | Total=%d | Valid=%d | Invalid=%d | Enabled=%d | Disabled=%d"),
        RegisteredLinks.Num(),
        ValidCount,
        InvalidCount,
        EnabledCount,
        DisabledCount);
}

TArray<APathLink*> UPathLinkSubsystem::GetAllLinks() const
{
    TArray<APathLink*> Result;
    Result.Reserve(RegisteredLinks.Num());

    for (const TWeakObjectPtr<APathLink>& WeakLink : RegisteredLinks)
    {
        if (APathLink* Link = WeakLink.Get(); IsValid(Link))
        {
            Result.Add(Link);
        }
    }

    return Result;
}

TArray<APathLink*> UPathLinkSubsystem::GetEnabledLinks() const
{
    TArray<APathLink*> Result;

    for (const TWeakObjectPtr<APathLink>& WeakLink : RegisteredLinks)
    {
        APathLink* Link = WeakLink.Get();
        if (IsValid(Link) && Link->IsUsable())
        {
            Result.Add(Link);
        }
    }

    return Result;
}


TArray<APathLink*> UPathLinkSubsystem::GetInvalidLinks() const
{
    TArray<APathLink*> Result;

    for (const TWeakObjectPtr<APathLink>& WeakLink : RegisteredLinks)
    {
        APathLink* Link = WeakLink.Get();
        if (IsValid(Link) && !Link->IsValidLink())
        {
            Result.Add(Link);
        }
    }

    return Result;
}

bool UPathLinkSubsystem::ValidateAllLinks(
    int32& OutValidCount,
    int32& OutInvalidCount) const
{
    OutValidCount = 0;
    OutInvalidCount = 0;

    for (const TWeakObjectPtr<APathLink>& WeakLink : RegisteredLinks)
    {
        APathLink* Link = WeakLink.Get();
        if (!IsValid(Link))
        {
            continue;
        }

        if (Link->ValidateAndLog())
        {
            ++OutValidCount;
        }
        else
        {
            ++OutInvalidCount;
        }
    }

    UE_LOG(
        LogPathLinkSubsystem,
        Log,
        TEXT("[PathLink][Validation] 검사 완료 | Valid=%d | Invalid=%d"),
        OutValidCount,
        OutInvalidCount);

    return OutInvalidCount == 0;
}

TArray<APathLink*> UPathLinkSubsystem::GetLinksByType(
    const EPathLinkType LinkType,
    const bool OnlyEnabled) const
{
    TArray<APathLink*> Result;

    for (const TWeakObjectPtr<APathLink>& WeakLink : RegisteredLinks)
    {
        APathLink* Link = WeakLink.Get();
        if (!IsValid(Link) || Link->GetLinkType() != LinkType)
        {
            continue;
        }

        if (OnlyEnabled && !Link->IsUsable())
        {
            continue;
        }

        Result.Add(Link);
    }

    return Result;
}

int32 UPathLinkSubsystem::GetLinkCount() const
{
    int32 Count = 0;
    for (const TWeakObjectPtr<APathLink>& WeakLink : RegisteredLinks)
    {
        if (WeakLink.IsValid())
        {
            ++Count;
        }
    }

    return Count;
}

APathLink* UPathLinkSubsystem::GetNearestLink(
    const FVector& Location,
    const bool OnlyEnabled) const
{
    APathLink* BestLink = nullptr;
    double BestDistanceSquared = TNumericLimits<double>::Max();

    for (const TWeakObjectPtr<APathLink>& WeakLink : RegisteredLinks)
    {
        APathLink* Link = WeakLink.Get();
        if (!IsValid(Link))
        {
            continue;
        }

        if (OnlyEnabled && !Link->IsUsable())
        {
            continue;
        }

        if (!Link->IsValidLink())
        {
            continue;
        }

        const double EntryDistanceSquared = FVector::DistSquared(Location, Link->GetEntryLocation());
        const double ExitDistanceSquared = FVector::DistSquared(Location, Link->GetExitLocation());
        const double LinkDistanceSquared = FMath::Min(EntryDistanceSquared, ExitDistanceSquared);

        if (LinkDistanceSquared < BestDistanceSquared)
        {
            BestDistanceSquared = LinkDistanceSquared;
            BestLink = Link;
        }
    }

    return BestLink;
}

bool UPathLinkSubsystem::ProjectToNavigation(
    const FVector& WorldLocation,
    FVector& OutNavLocation) const
{
    OutNavLocation = WorldLocation;

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return false;
    }

    UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World);
    if (!IsValid(NavSystem))
    {
        return false;
    }

    FNavLocation ProjectedLocation;
    const FVector ProjectionExtent(150.0, 150.0, 400.0);

    if (!NavSystem->ProjectPointToNavigation(
        WorldLocation,
        ProjectedLocation,
        ProjectionExtent,
        static_cast<const FNavAgentProperties*>(nullptr),
        FSharedConstNavQueryFilter()))
    {
        return false;
    }

    OutNavLocation = ProjectedLocation.Location;
    return true;
}

bool UPathLinkSubsystem::GetNavPathDistance(
    const FVector& StartLocation,
    const FVector& TargetLocation,
    double& OutDistance,
    AActor* PathfindingContext) const
{
    OutDistance = 0.0;

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return false;
    }

    UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
        World,
        StartLocation,
        TargetLocation,
        PathfindingContext,
        nullptr);

    if (!IsValid(Path) || !Path->IsValid() || Path->IsPartial())
    {
        return false;
    }

    OutDistance = Path->GetPathLength();
    return true;
}

void UPathLinkSubsystem::DrawDebugRoute(
    const TArray<FPathLinkRouteSegment>& RouteSegments,
    const float Duration,
    const float Thickness,
    const bool PersistentLines)
{
    UWorld* World = GetWorld();
    if (!IsValid(World) || RouteSegments.IsEmpty())
    {
        return;
    }

    // NavMesh 표면과 완전히 겹치면 선이 깜빡이거나 묻힐 수 있으므로 약간 위에 표시합니다.
    constexpr float HeightOffset = 10.0f;
    const FVector DebugOffset(0.0f, 0.0f, HeightOffset);

    // NavMesh 일반 이동 구간은 Link 타입 색상과 구분되도록 흰색으로 고정합니다.
    const FColor NormalRouteColor = FColor::White;

    const auto GetLinkRouteColor = [](const EPathLinkType LinkType)
    {
        switch (LinkType)
        {
        case EPathLinkType::Teleport:
            return FColor(170, 80, 255); // 보라색
        case EPathLinkType::JumpPad:
            return FColor(60, 220, 90);  // 초록색
        case EPathLinkType::Jump:
            return FColor(255, 220, 40); // 노란색
        case EPathLinkType::Drop:
            return FColor(70, 140, 255); // 파란색
        default:
            return FColor::White;
        }
    };

    for (const FPathLinkRouteSegment& Segment : RouteSegments)
    {
        if (Segment.SegmentType == EPathLinkSegmentType::Normal)
        {
            // RouteFinder가 저장해 둔 실제 NavMesh PathPoints를 그대로 따라 그립니다.
            if (Segment.PathPoints.Num() >= 2)
            {
                for (int32 PointIndex = 0; PointIndex < Segment.PathPoints.Num() - 1; ++PointIndex)
                {
                    DrawDebugLine(
                        World,
                        Segment.PathPoints[PointIndex] + DebugOffset,
                        Segment.PathPoints[PointIndex + 1] + DebugOffset,
                        NormalRouteColor,
                        PersistentLines,
                        Duration,
                        0,
                        Thickness);
                }
            }
            else
            {
                // 예외적으로 PathPoints가 비어 있어도 Segment 자체의 시작/끝 위치는 시각화합니다.
                DrawDebugLine(
                    World,
                    Segment.StartLocation + DebugOffset,
                    Segment.EndLocation + DebugOffset,
                    NormalRouteColor,
                    PersistentLines,
                    Duration,
                    0,
                    Thickness);
            }
        }
        else
        {
            // Link 구간은 실제 선택된 이동 방향(Reverse 포함)의 Entry -> Exit를 타입 고정 색상으로 표시합니다.
            DrawDebugLine(
                World,
                Segment.StartLocation + DebugOffset,
                Segment.EndLocation + DebugOffset,
                GetLinkRouteColor(Segment.LinkType),
                PersistentLines,
                Duration,
                0,
                Thickness + 2.0f);
        }
    }
}

bool UPathLinkSubsystem::FindShortestRoute(
    const FVector& StartLocation,
    const FVector& TargetLocation,
    FPathLinkRouteResult& OutResult,
    AActor* PathfindingContext) const
{
    OutResult.Reset();

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return false;
    }

    // AI/NavMesh 기반 Route 계산은 서버(또는 Standalone)에서만 수행합니다.
    // Client World에는 NavigationSystem/NavData가 없을 수 있으며 이는 오류가 아닙니다.
    if (World->GetNetMode() == NM_Client)
    {
        return false;
    }

    const FPathLinkStaticGraph* RouteGraph = nullptr;
    if (!EnsureRouteGraphCache(PathfindingContext, RouteGraph))
    {
        if (bCachedHasBlockingDuplicates)
        {
            UE_LOG(
                LogPathLinkSubsystem,
                Error,
                TEXT("[PathLink][BLOCKED][DuplicatePlacement] FindShortestRoute 실행 차단 | 중복 Link를 먼저 수정하세요.\n%s"),
                *CachedDuplicateSummary);
        }
        return false;
    }

    if (!RouteGraph)
    {
        return false;
    }

    FPathLinkRouteFinder RouteFinder(World);
    return RouteFinder.FindShortestRoute(
        StartLocation,
        TargetLocation,
        *RouteGraph,
        PathfindingContext,
        OutResult);
}

bool UPathLinkSubsystem::FindShortestRouteToActor(
    const FVector& StartLocation,
    AActor* TargetActor,
    FPathLinkRouteResult& OutResult,
    AActor* PathfindingContext) const
{
    if (!IsValid(TargetActor))
    {
        OutResult.Reset();
        return false;
    }

    return FindShortestRoute(
        StartLocation,
        TargetActor->GetActorLocation(),
        OutResult,
        PathfindingContext);
}

bool UPathLinkSubsystem::GetRouteDistance(
    const FVector& StartLocation,
    const FVector& TargetLocation,
    double& OutDistance,
    AActor* PathfindingContext) const
{
    OutDistance = 0.0;

    FPathLinkRouteResult RouteResult;
    if (!FindShortestRoute(StartLocation, TargetLocation, RouteResult, PathfindingContext))
    {
        return false;
    }

    OutDistance = RouteResult.TotalDistance;
    return true;
}

bool UPathLinkSubsystem::CanReach(
    const FVector& StartLocation,
    const FVector& TargetLocation,
    AActor* PathfindingContext) const
{
    FPathLinkRouteResult RouteResult;
    return FindShortestRoute(StartLocation, TargetLocation, RouteResult, PathfindingContext);
}

void UPathLinkSubsystem::InvalidateRouteGraphCache()
{
    bRouteGraphCacheDirty = true;
    bCommonRouteCacheReady = false;
    bDefaultRouteGraphReady = false;
    bCachedHasBlockingDuplicates = false;
    CachedDuplicateSummary.Reset();
    CachedRouteLinks.Reset();
    DefaultRouteGraph.Reset();
    ContextRouteGraphs.Reset();
}

void UPathLinkSubsystem::RebuildCommonRouteCache() const
{
    CachedRouteLinks.Reset();
    DefaultRouteGraph.Reset();
    ContextRouteGraphs.Reset();
    bDefaultRouteGraphReady = false;
    bCachedHasBlockingDuplicates = false;
    CachedDuplicateSummary.Reset();

    TArray<APathLink*> DuplicateLinks;
    bCachedHasBlockingDuplicates = HasBlockingDuplicateLinks(DuplicateLinks, CachedDuplicateSummary);

    // Duplicate는 전체 Route를 차단하므로 개별 Link Validation에서 다시 O(L) 검색할 필요가 없습니다.
    // 여기서는 Duplicate를 제외한 구조 Validation만 Link당 한 번 수행합니다.
    for (const TWeakObjectPtr<APathLink>& WeakLink : RegisteredLinks)
    {
        APathLink* Link = WeakLink.Get();
        if (!IsValid(Link) || !Link->IsEnabled())
        {
            continue;
        }

        if (!Link->IsValidLinkForRouteCache())
        {
            continue;
        }

        CachedRouteLinks.Add(Link);
    }

    bRouteGraphCacheDirty = false;
    bCommonRouteCacheReady = true;

    UE_LOG(
        LogPathLinkSubsystem,
        Log,
        TEXT("[PathLink][RouteCache] Common Cache 구축 | Registered=%d | RouteCandidates=%d | DuplicateBlocked=%s"),
        RegisteredLinks.Num(),
        CachedRouteLinks.Num(),
        bCachedHasBlockingDuplicates ? TEXT("true") : TEXT("false"));
}

TArray<APathLink*> UPathLinkSubsystem::GetCachedRouteLinks() const
{
    TArray<APathLink*> Result;
    Result.Reserve(CachedRouteLinks.Num());

    for (const TWeakObjectPtr<APathLink>& WeakLink : CachedRouteLinks)
    {
        if (APathLink* Link = WeakLink.Get(); IsValid(Link))
        {
            Result.Add(Link);
        }
    }

    return Result;
}

bool UPathLinkSubsystem::EnsureRouteGraphCache(
    AActor* PathfindingContext,
    const FPathLinkStaticGraph*& OutGraph) const
{
    OutGraph = nullptr;

    UWorld* World = GetWorld();
    if (!IsValid(World) || World->GetNetMode() == NM_Client)
    {
        return false;
    }

    if (bRouteGraphCacheDirty || !bCommonRouteCacheReady)
    {
        RebuildCommonRouteCache();
    }

    if (bCachedHasBlockingDuplicates)
    {
        return false;
    }

    const TArray<APathLink*> RouteLinks = GetCachedRouteLinks();
    FPathLinkRouteFinder RouteFinder(World);

    if (!IsValid(PathfindingContext))
    {
        if (!bDefaultRouteGraphReady)
        {
            FPathLinkStaticGraph NewGraph;
            if (!RouteFinder.BuildStaticGraph(RouteLinks, nullptr, NewGraph))
            {
                return false;
            }

            DefaultRouteGraph = MoveTemp(NewGraph);
            bDefaultRouteGraphReady = true;
        }

        OutGraph = &DefaultRouteGraph;
        return true;
    }

    // 파괴된 AI Context Cache는 가볍게 정리합니다.
    for (auto It = ContextRouteGraphs.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            It.RemoveCurrent();
        }
    }

    const TWeakObjectPtr<AActor> ContextKey(PathfindingContext);
    if (FPathLinkStaticGraph* ExistingGraph = ContextRouteGraphs.Find(ContextKey))
    {
        OutGraph = ExistingGraph;
        return true;
    }

    FPathLinkStaticGraph NewGraph;
    if (!RouteFinder.BuildStaticGraph(RouteLinks, PathfindingContext, NewGraph))
    {
        return false;
    }

    FPathLinkStaticGraph& StoredGraph = ContextRouteGraphs.Add(ContextKey, MoveTemp(NewGraph));
    OutGraph = &StoredGraph;
    return true;
}

bool UPathLinkSubsystem::HasBlockingDuplicateLinks(
    TArray<APathLink*>& OutDuplicateLinks,
    FString& OutSummary) const
{
    OutDuplicateLinks.Reset();
    OutSummary.Reset();

    TSet<const APathLink*> AddedLinks;
    TArray<FString> SummaryLines;

    for (const TWeakObjectPtr<APathLink>& WeakLink : RegisteredLinks)
    {
        APathLink* Link = WeakLink.Get();
        if (!IsValid(Link))
        {
            continue;
        }

        FString Details;
        if (!Link->HasDuplicatePlacementError(Details))
        {
            continue;
        }

        if (!AddedLinks.Contains(Link))
        {
            AddedLinks.Add(Link);
            OutDuplicateLinks.Add(Link);
        }

        SummaryLines.Add(FString::Printf(
            TEXT("- %s (%s)\n  %s"),
            *Link->GetName(),
            *UEnum::GetValueAsString(Link->GetLinkType()),
            *Details.Replace(TEXT("\n"), TEXT("\n  "))));
    }

    if (OutDuplicateLinks.IsEmpty())
    {
        return false;
    }

    OutSummary = FString::Join(SummaryLines, TEXT("\n"));
    return true;
}

#if WITH_EDITOR
void UPathLinkSubsystem::BlockEditorPlayForDuplicates(const FString& Summary) const
{
    // 로그만 보고 지나치는 상황을 막기 위해 Modal 안내를 띄웁니다.
    // 확인 버튼을 누르면 현재 PIE/SIE를 즉시 종료합니다.
    const FText Message = FText::FromString(FString::Printf(
        TEXT("중복 배치된 PathLink가 발견되어 실행을 중단합니다.\n\n")
        TEXT("중복 Link를 제거하거나 TwoWay 설정을 수정한 뒤 다시 실행하세요.\n\n")
        TEXT("%s"),
        *Summary));

    FMessageDialog::Open(EAppMsgType::Ok, Message);

    if (GEditor)
    {
        GEditor->RequestEndPlayMap();
    }
}
#endif

void UPathLinkSubsystem::RegisterLink(APathLink* Link)
{
    if (!IsValid(Link))
    {
        return;
    }

    // Streaming 재진입이나 RefreshLinks와 BeginPlay가 겹쳐도 중복 등록되지 않습니다.
    RegisteredLinks.RemoveAll(
        [](const TWeakObjectPtr<APathLink>& WeakLink)
        {
            return !WeakLink.IsValid();
        });

    const bool AlreadyRegistered = RegisteredLinks.ContainsByPredicate(
        [Link](const TWeakObjectPtr<APathLink>& WeakLink)
        {
            return WeakLink.Get() == Link;
        });

    if (!AlreadyRegistered)
    {
        RegisteredLinks.Add(Link);
        InvalidateRouteGraphCache();

        // 일반 구조 Invalid Link는 Registry에는 남겨 디버깅/조회할 수 있게 하되
        // IsUsable()에 의해 활성 Link 조회에서 제외됩니다. 실제 NavMesh 사용 가능 여부는 Route 계산 시 별도로 검사합니다.
        // DuplicatePlacement는 별도 Blocking Error로 처리되어 PIE/SIE와 Route 계산까지 차단됩니다.
        // 등록 시 상세 오류를 출력해 어떤 Link의 어느 부분이 잘못됐는지 바로 확인할 수 있게 합니다.
        Link->LogValidationErrors();
    }
}

void UPathLinkSubsystem::UnregisterLink(APathLink* Link)
{
    const int32 RemovedCount = RegisteredLinks.RemoveAll(
        [Link](const TWeakObjectPtr<APathLink>& WeakLink)
        {
            return !WeakLink.IsValid() || WeakLink.Get() == Link;
        });

    if (RemovedCount > 0)
    {
        InvalidateRouteGraphCache();
    }
}
