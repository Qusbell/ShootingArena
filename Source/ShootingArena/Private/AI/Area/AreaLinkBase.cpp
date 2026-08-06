#include "AI/Area/AreaLinkBase.h"

#include "AI/Area/AIAreaBase.h"
#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

AAreaLinkBase::AAreaLinkBase()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    AreaAPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("AreaAPoint"));
    AreaAPoint->SetupAttachment(SceneRoot);
    AreaAPoint->SetRelativeLocation(FVector(-100.0f, 0.0f, 0.0f));
    AreaAPoint->ArrowColor = FColor::Cyan;
    AreaAPoint->ArrowSize = 1.5f;
    AreaAPoint->SetHiddenInGame(true);

    AreaBPoint = CreateDefaultSubobject<UArrowComponent>(TEXT("AreaBPoint"));
    AreaBPoint->SetupAttachment(SceneRoot);
    AreaBPoint->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));
    AreaBPoint->ArrowColor = FColor::Green;
    AreaBPoint->ArrowSize = 1.5f;
    AreaBPoint->SetHiddenInGame(true);
}

void AAreaLinkBase::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

#if WITH_EDITOR
    // Endpoint 자동 계산은 에디터 미리보기/Bake에서만 수행합니다.
    // PIE와 패키징 런타임의 Actor Construction에서는 Nav 조회를 만들지 않습니다.
    if (UWorld* World = GetWorld(); IsValid(World) && !World->IsGameWorld())
    {
        UpdateEndpointPreview();
    }
#endif
}

bool AAreaLinkBase::IsValidLink() const
{
    if (!bLinkEnabled)
    {
        return false;
    }

    switch (EndpointMode)
    {
    case EAreaLinkEndpointMode::ActorReferences:
        return IsValid(GetEffectiveEntryActor()) && IsValid(ExitActor);

    case EAreaLinkEndpointMode::Automatic:
        switch (TraversalType)
        {
        case EAreaTraversalType::Teleport:
        case EAreaTraversalType::JumpPad:
        case EAreaTraversalType::Normal:
            // 목적지를 임의로 추측할 수 없으므로 출구 Actor가 필요합니다.
            return IsValid(GetEffectiveEntryActor()) && IsValid(ExitActor);

        case EAreaTraversalType::Drop:
        case EAreaTraversalType::Jump:
        case EAreaTraversalType::Door:
            // 이 종류는 Link Actor 자체를 기준점으로 사용할 수 있습니다.
            return true;

        default:
            return false;
        }

    case EAreaLinkEndpointMode::ManualOverride:
        return IsValid(AreaAPoint) && IsValid(AreaBPoint);

    default:
        return false;
    }
}

bool AAreaLinkBase::ResolveEndpointLocations(
    FVector& OutEntryLocation,
    FVector& OutExitLocation,
    FText& OutFailureReason) const
{
    FString FailureReason;
    const bool bResolved = TryResolveEndpointLocations(
        GetWorld(),
        OutEntryLocation,
        OutExitLocation,
        FailureReason);

    OutFailureReason = bResolved
        ? FText::GetEmpty()
        : FText::FromString(FailureReason);

    return bResolved;
}

void AAreaLinkBase::RefreshEndpointPreview()
{
    UpdateEndpointPreview();
}

bool AAreaLinkBase::TryResolveEndpointLocations(
    UWorld* World,
    FVector& OutEntryLocation,
    FVector& OutExitLocation,
    FString& OutFailureReason) const
{
    OutEntryLocation = FVector::ZeroVector;
    OutExitLocation = FVector::ZeroVector;
    OutFailureReason.Reset();

    if (!bLinkEnabled)
    {
        OutFailureReason = TEXT("Link Enabled가 false입니다.");
        return false;
    }

    bool bResolved = false;

    switch (EndpointMode)
    {
    case EAreaLinkEndpointMode::ActorReferences:
        bResolved = TryResolveActorReferenceEndpoints(
            OutEntryLocation,
            OutExitLocation,
            OutFailureReason);
        break;

    case EAreaLinkEndpointMode::Automatic:
        bResolved = TryResolveAutomaticEndpoints(
            World,
            OutEntryLocation,
            OutExitLocation,
            OutFailureReason);
        break;

    case EAreaLinkEndpointMode::ManualOverride:
        if (!IsValid(AreaAPoint) || !IsValid(AreaBPoint))
        {
            OutFailureReason = TEXT("Manual Override Point Component가 유효하지 않습니다.");
            return false;
        }

        OutEntryLocation = AreaAPoint->GetComponentLocation();
        OutExitLocation = AreaBPoint->GetComponentLocation();
        bResolved = true;
        break;

    default:
        OutFailureReason = TEXT("지원하지 않는 Endpoint Mode입니다.");
        return false;
    }

    if (!bResolved)
    {
        return false;
    }

    if (OutEntryLocation.Equals(OutExitLocation, 1.0f))
    {
        OutFailureReason = TEXT("Entry와 Exit 위치가 같습니다.");
        return false;
    }

    return true;
}

FVector AAreaLinkBase::GetEntryLocationAtoB() const
{
    FVector EntryLocation;
    FVector ExitLocation;
    FString FailureReason;

    if (TryResolveEndpointLocations(GetWorld(), EntryLocation, ExitLocation, FailureReason))
    {
        return EntryLocation;
    }

    return IsValid(AreaAPoint) ? AreaAPoint->GetComponentLocation() : GetActorLocation();
}

FVector AAreaLinkBase::GetExitLocationAtoB() const
{
    FVector EntryLocation;
    FVector ExitLocation;
    FString FailureReason;

    if (TryResolveEndpointLocations(GetWorld(), EntryLocation, ExitLocation, FailureReason))
    {
        return ExitLocation;
    }

    return IsValid(AreaBPoint) ? AreaBPoint->GetComponentLocation() : GetActorLocation();
}

EAreaTraversalType AAreaLinkBase::GetTraversalTypeBtoA() const
{
    // 높이 방향이 반대로 바뀌면 일반 점프와 낙하는 서로 반대 행동이 됩니다.
    switch (TraversalType)
    {
    case EAreaTraversalType::Jump:
        return EAreaTraversalType::Drop;

    case EAreaTraversalType::Drop:
        return EAreaTraversalType::Jump;

    default:
        return TraversalType;
    }
}

FVector AAreaLinkBase::GetEntryLocationBtoA() const
{
    // 역방향 전용 Actor가 있으면 실제 이동 시스템이 향할 Actor 위치를 진입점으로 사용합니다.
    if (IsValid(ReverseTraversalActor))
    {
        return ReverseTraversalActor->GetActorLocation();
    }

    return GetExitLocationAtoB();
}

FVector AAreaLinkBase::GetExitLocationBtoA() const
{
    return GetEntryLocationAtoB();
}

AActor* AAreaLinkBase::GetEffectiveEntryActor() const
{
    if (IsValid(EntryActor))
    {
        return EntryActor;
    }

    return IsValid(TraversalActor) ? TraversalActor.Get() : nullptr;
}

bool AAreaLinkBase::TryResolveActorReferenceEndpoints(
    FVector& OutEntryLocation,
    FVector& OutExitLocation,
    FString& OutFailureReason) const
{
    AActor* EffectiveEntryActor = GetEffectiveEntryActor();
    if (!IsValid(EffectiveEntryActor))
    {
        OutFailureReason = TEXT("EntryActor와 TraversalActor가 모두 비어 있습니다.");
        return false;
    }

    if (!IsValid(ExitActor))
    {
        OutFailureReason = TEXT("ExitActor가 비어 있습니다.");
        return false;
    }

    OutEntryLocation = EffectiveEntryActor->GetActorLocation();
    OutExitLocation = ExitActor->GetActorLocation();
    return true;
}

bool AAreaLinkBase::TryResolveAutomaticEndpoints(
    UWorld* World,
    FVector& OutEntryLocation,
    FVector& OutExitLocation,
    FString& OutFailureReason) const
{
    if (!IsValid(World))
    {
        OutFailureReason = TEXT("Endpoint 자동 계산에 사용할 World가 없습니다.");
        return false;
    }

    AActor* EffectiveEntryActor = GetEffectiveEntryActor();
    const FVector EntryLocation = IsValid(EffectiveEntryActor)
        ? EffectiveEntryActor->GetActorLocation()
        : GetActorLocation();

    OutEntryLocation = EntryLocation;

    switch (TraversalType)
    {
    case EAreaTraversalType::Teleport:
    case EAreaTraversalType::JumpPad:
    case EAreaTraversalType::Normal:
        // 임의의 텔레포트 출구나 점프패드 착지점을 추측하면 잘못된 경로가 생성될 수 있습니다.
        // 따라서 Entry는 TraversalActor/EntryActor로 자동 선택하되, ExitActor는 명시적으로 받습니다.
        if (!IsValid(ExitActor))
        {
            OutFailureReason = TEXT("이 Traversal Type의 Automatic 모드에는 ExitActor가 필요합니다.");
            return false;
        }

        OutExitLocation = ExitActor->GetActorLocation();
        return true;

    case EAreaTraversalType::Drop:
    {
        const FVector TraceStart = EntryLocation + FVector::UpVector * AutomaticTraceStartHeight;
        const FVector TraceEnd = EntryLocation - FVector::UpVector * AutomaticDropTraceDistance;

        return TryTraceAndProjectLandingPoint(
            World,
            TraceStart,
            TraceEnd,
            OutExitLocation,
            OutFailureReason);
    }

    case EAreaTraversalType::Jump:
    {
        FVector Forward = IsValid(EffectiveEntryActor)
            ? EffectiveEntryActor->GetActorForwardVector()
            : GetActorForwardVector();

        Forward.Z = 0.0f;
        if (!Forward.Normalize())
        {
            OutFailureReason = TEXT("Jump 자동 계산에 사용할 전방 방향이 유효하지 않습니다.");
            return false;
        }

        const FVector LandingCandidate = EntryLocation
            + Forward * AutomaticJumpForwardDistance
            + FVector::UpVector * AutomaticJumpHeightOffset;

        const FVector TraceStart = LandingCandidate + FVector::UpVector * AutomaticJumpTraceUpDistance;
        const FVector TraceEnd = LandingCandidate - FVector::UpVector * AutomaticJumpTraceDownDistance;

        return TryTraceAndProjectLandingPoint(
            World,
            TraceStart,
            TraceEnd,
            OutExitLocation,
            OutFailureReason);
    }

    case EAreaTraversalType::Door:
    {
        FVector Forward = IsValid(EffectiveEntryActor)
            ? EffectiveEntryActor->GetActorForwardVector()
            : GetActorForwardVector();

        Forward.Z = 0.0f;
        if (!Forward.Normalize())
        {
            OutFailureReason = TEXT("Door 자동 계산에 사용할 전방 방향이 유효하지 않습니다.");
            return false;
        }

        const FVector RawEntry = EntryLocation - Forward * AutomaticDoorHalfWidth;
        const FVector RawExit = EntryLocation + Forward * AutomaticDoorHalfWidth;

        FVector ProjectedEntry;
        FVector ProjectedExit;
        if (!TryProjectPointToNavigation(World, RawEntry, ProjectedEntry)
            || !TryProjectPointToNavigation(World, RawExit, ProjectedExit))
        {
            OutFailureReason = TEXT("Door 양쪽 Endpoint를 NavMesh 위로 투영하지 못했습니다.");
            return false;
        }

        OutEntryLocation = ProjectedEntry;
        OutExitLocation = ProjectedExit;
        return true;
    }

    default:
        OutFailureReason = TEXT("지원하지 않는 Automatic Traversal Type입니다.");
        return false;
    }
}

bool AAreaLinkBase::TryTraceAndProjectLandingPoint(
    UWorld* World,
    const FVector& TraceStart,
    const FVector& TraceEnd,
    FVector& OutLandingLocation,
    FString& OutFailureReason) const
{
    if (!IsValid(World))
    {
        OutFailureReason = TEXT("바닥 Trace에 사용할 World가 없습니다.");
        return false;
    }

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AreaLinkAutomaticEndpointTrace), false, this);
    QueryParams.AddIgnoredActor(this);

    if (IsValid(TraversalActor))
    {
        QueryParams.AddIgnoredActor(TraversalActor.Get());
    }

    if (IsValid(EntryActor))
    {
        QueryParams.AddIgnoredActor(EntryActor.Get());
    }

    FHitResult HitResult;
    const bool bHit = World->LineTraceSingleByChannel(
        HitResult,
        TraceStart,
        TraceEnd,
        AutomaticTraceChannel.GetValue(),
        QueryParams);

    if (!bHit)
    {
        OutFailureReason = TEXT("자동 Endpoint 계산 중 착지 바닥을 찾지 못했습니다.");
        return false;
    }

    if (!TryProjectPointToNavigation(World, HitResult.ImpactPoint, OutLandingLocation))
    {
        OutFailureReason = TEXT("찾은 착지 지점을 NavMesh 위로 투영하지 못했습니다.");
        return false;
    }

    return true;
}

bool AAreaLinkBase::TryProjectPointToNavigation(
    UWorld* World,
    const FVector& Point,
    FVector& OutProjectedPoint) const
{
    if (!IsValid(World))
    {
        return false;
    }

    UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!IsValid(NavigationSystem))
    {
        return false;
    }

    FNavLocation ProjectedLocation;
    if (!NavigationSystem->ProjectPointToNavigation(
        Point,
        ProjectedLocation,
        NavigationProjectionExtent))
    {
        return false;
    }

    OutProjectedPoint = ProjectedLocation.Location;
    return true;
}

void AAreaLinkBase::UpdateEndpointPreview()
{
    if (!IsValid(AreaAPoint) || !IsValid(AreaBPoint))
    {
        return;
    }

    // Manual Override에서는 사용자가 직접 옮긴 Component 위치를 유지해야 합니다.
    if (EndpointMode == EAreaLinkEndpointMode::ManualOverride)
    {
        return;
    }

    FVector EntryLocation;
    FVector ExitLocation;
    FString FailureReason;
    if (!TryResolveEndpointLocations(GetWorld(), EntryLocation, ExitLocation, FailureReason))
    {
        return;
    }

    // Actor 참조 또는 자동 계산 결과를 에디터에서 바로 확인할 수 있도록 Arrow를 맞춥니다.
    AreaAPoint->SetWorldLocation(EntryLocation);
    AreaBPoint->SetWorldLocation(ExitLocation);
}
