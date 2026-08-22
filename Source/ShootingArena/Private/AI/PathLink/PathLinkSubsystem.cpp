#include "AI/PathLink/PathLinkSubsystem.h"

#include "AI/PathLink/PathLink.h"
#include "AI/PathLink/Internal/PathLinkRouteFinder.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathLinkSubsystem, Log, All);

void UPathLinkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    RegisteredLinks.Reset();
}

void UPathLinkSubsystem::Deinitialize()
{
    RegisteredLinks.Reset();
    Super::Deinitialize();
}

void UPathLinkSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    // BeginPlay 호출 순서에 상관없이 현재 레벨의 Link를 한 번 확실하게 수집합니다.
    // 이후 Streaming으로 새로 들어오는 Link는 각 APathLink::BeginPlay에서 자동 등록됩니다.
    RefreshLinks();
}

void UPathLinkSubsystem::RefreshLinks()
{
    RegisteredLinks.Reset();

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

        if (Link->LogValidationErrors())
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

    const TArray<APathLink*> EnabledLinks = GetEnabledLinks();

    FPathLinkRouteFinder RouteFinder(World);
    return RouteFinder.FindShortestRoute(
        StartLocation,
        TargetLocation,
        EnabledLinks,
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

        // Invalid Link는 Registry에는 남겨 디버깅/조회할 수 있게 하되,
        // 길찾기 후보에서는 IsUsable()에 의해 자동 제외됩니다.
        // 등록 시 한 번 상세 오류를 출력해 어떤 Link의 어느 부분이 잘못됐는지 바로 확인할 수 있게 합니다.
        Link->LogValidationErrors();
    }
}

void UPathLinkSubsystem::UnregisterLink(APathLink* Link)
{
    RegisteredLinks.RemoveAll(
        [Link](const TWeakObjectPtr<APathLink>& WeakLink)
        {
            return !WeakLink.IsValid() || WeakLink.Get() == Link;
        });
}
