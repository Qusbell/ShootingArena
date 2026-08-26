#include "AI/PathLink/PathLink.h"

#include "AI/PathLink/PathLinkSubsystem.h"
#include "Components/SceneComponent.h"
#if WITH_EDITORONLY_DATA
#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"
#endif
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathLink, Log, All);

namespace PathLinkPlacement
{
    constexpr float GroundTraceStartHeight = 50.0f;
    constexpr float GroundTraceDistance = 100000.0f;
    constexpr float GroundTolerance = 10.0f;
    constexpr float DuplicatePointTolerance = 25.0f;
}

namespace
{
    FVector AddLocalOffset(const AActor* Actor, const FVector& BaseLocation, const FVector& LocalOffset)
    {
        if (!IsValid(Actor) || LocalOffset.IsNearlyZero())
        {
            return BaseLocation;
        }

        return BaseLocation + Actor->GetActorTransform().TransformVectorNoScale(LocalOffset);
    }

    FString LinkTypeToString(const EPathLinkType LinkType)
    {
        if (const UEnum* Enum = StaticEnum<EPathLinkType>())
        {
            return Enum->GetNameStringByValue(static_cast<int64>(LinkType));
        }

        return TEXT("Unknown");
    }

    /** "[Part] Message" 형식에서 Part와 Message를 분리해 Output Log를 읽기 쉽게 만듭니다. */
    void SplitValidationError(const FString& Error, FString& OutPart, FString& OutMessage)
    {
        OutPart = TEXT("Unknown");
        OutMessage = Error;

        if (!Error.StartsWith(TEXT("[")))
        {
            return;
        }

        int32 CloseBracketIndex = INDEX_NONE;
        if (!Error.FindChar(TEXT(']'), CloseBracketIndex) || CloseBracketIndex <= 1)
        {
            return;
        }

        OutPart = Error.Mid(1, CloseBracketIndex - 1);
        OutMessage = Error.Mid(CloseBracketIndex + 1).TrimStartAndEnd();
    }
}

APathLink::APathLink()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

#if WITH_EDITORONLY_DATA
    // SceneComponent만 있으면 레벨 뷰포트에서 PathLink를 직접 클릭하기 어렵습니다.
    // Editor 전용 Billboard를 두어 World Outliner를 거치지 않고 뷰포트에서 바로 선택할 수 있게 합니다.
    EditorIcon = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
    if (EditorIcon)
    {
        EditorIcon->SetupAttachment(SceneRoot);
        EditorIcon->SetRelativeLocation(FVector(0.0f, 0.0f, 30.0f));
        EditorIcon->SetHiddenInGame(true);
        EditorIcon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        EditorIcon->bIsScreenSizeScaled = true;
        EditorIcon->ScreenSize = 0.0025f;
        EditorIcon->bIsEditorOnly = true;

        // Engine에 기본 포함된 Editor 아이콘을 사용합니다. 별도 프로젝트 리소스가 필요하지 않습니다.
        static ConstructorHelpers::FObjectFinderOptional<UTexture2D> PathLinkIconTexture(
            TEXT("/Engine/EditorResources/S_Actor.S_Actor"));
        if (PathLinkIconTexture.Succeeded())
        {
            EditorIcon->SetSprite(PathLinkIconTexture.Get());
        }
    }
#endif

    SetActorEnableCollision(false);
    SetReplicates(false);
}

void APathLink::BeginPlay()
{
    Super::BeginPlay();

    // 런타임에는 Visual Tick이 필요하지 않습니다.
    SetActorTickEnabled(false);

    // 레벨에 배치된 Link는 자기 자신을 현재 World의 Subsystem에 자동 등록합니다.
    if (UWorld* World = GetWorld())
    {
        if (UPathLinkSubsystem* Subsystem = World->GetSubsystem<UPathLinkSubsystem>())
        {
            Subsystem->RegisterLink(this);
        }
    }
}

void APathLink::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        if (UPathLinkSubsystem* Subsystem = World->GetSubsystem<UPathLinkSubsystem>())
        {
            Subsystem->UnregisterLink(this);
        }
    }

    Super::EndPlay(EndPlayReason);
}

void APathLink::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

#if WITH_EDITOR
    UWorld* World = GetWorld();
    if (IsValid(World) && !World->IsGameWorld())
    {
        DrawEditorVisual();
    }
#endif
}

void APathLink::AttachToGround()
{
#if WITH_EDITOR
    UWorld* World = GetWorld();
    if (!IsValid(World) || World->IsGameWorld())
    {
        UE_LOG(
            LogPathLink,
            Error,
            TEXT("[PathLink][AttachToGround] Link=%s | Error=Editor World에서만 사용할 수 있습니다."),
            *GetName());
        return;
    }

    FHitResult GroundHit;
    if (!TraceGround(GroundHit))
    {
        UE_LOG(
            LogPathLink,
            Error,
            TEXT("[PathLink][AttachToGround] Link=%s | Error=현재 위치 아래에서 지면을 찾지 못했습니다."),
            *GetName());
        return;
    }

    Modify();
    SetActorLocation(GroundHit.ImpactPoint, false, nullptr, ETeleportType::TeleportPhysics);
    MarkPackageDirty();

    UE_LOG(
        LogPathLink,
        Log,
        TEXT("[PathLink][AttachToGround] Link=%s | Ground=%s | Location=%s"),
        *GetName(),
        *GetNameSafe(GroundHit.GetActor()),
        *GroundHit.ImpactPoint.ToCompactString());
#else
    UE_LOG(
        LogPathLink,
        Warning,
        TEXT("[PathLink][AttachToGround] Link=%s | Editor 전용 기능입니다."),
        *GetName());
#endif
}

bool APathLink::IsValidLink() const
{
    TArray<FString> Errors;
    CollectValidationErrors(Errors, true);
    return Errors.IsEmpty();
}

bool APathLink::IsValidLinkForRouteCache() const
{
    // ExitActor가 없는 PathLink는 다른 Link의 Exit Marker로는 사용할 수 있지만
    // 자기 자신이 이동 Link가 되는 것은 아니므로 Route Cache 후보에서 제외합니다.
    if (!IsValid(ExitActor))
    {
        return false;
    }

    TArray<FString> Errors;
    CollectValidationErrors(Errors, false);
    return Errors.IsEmpty();
}

bool APathLink::ValidateLink(FText& OutFailureReason) const
{
    TArray<FString> Errors;
    CollectValidationErrors(Errors, true);

    if (Errors.IsEmpty())
    {
        OutFailureReason = FText::GetEmpty();
        return true;
    }

    OutFailureReason = FText::FromString(FString::Join(Errors, TEXT("\n")));
    return false;
}

bool APathLink::ValidateAndLog() const
{
    TArray<FString> Errors;
    CollectValidationErrors(Errors, true);

    // 구조가 정상일 때만 Navigation 상태를 추가 검사합니다.
    // Client World는 AI Route 계산 주체가 아니며 NavData가 없을 수 있으므로 Navigation 오류로 취급하지 않습니다.
    if (Errors.IsEmpty())
    {
        if (const UWorld* World = GetWorld(); IsValid(World) && World->GetNetMode() != NM_Client)
        {
            TArray<FString> NavigationErrors;
            CollectNavigationErrors(NavigationErrors);
            Errors.Append(NavigationErrors);
        }
    }

    const FString TypeName = LinkTypeToString(LinkType);

    if (Errors.IsEmpty())
    {
        if (!IsValid(ExitActor))
        {
            UE_LOG(
                LogPathLink,
                Log,
                TEXT("[PathLink][MARKER] Link=%s | Entry=Self(%s) | ExitActor=None | RouteCandidate=false"),
                *GetName(),
                *GetActorLocation().ToCompactString());
            return true;
        }

        UE_LOG(
            LogPathLink,
            Log,
            TEXT("[PathLink][VALID] Link=%s | Type=%s | Entry=Self(%s) | ExitActor=%s | TwoWay=%s | Enabled=%s"),
            *GetName(),
            *TypeName,
            *GetActorLocation().ToCompactString(),
            *GetNameSafe(ExitActor.Get()),
            TwoWay ? TEXT("true") : TEXT("false"),
            Enabled ? TEXT("true") : TEXT("false"));
        return true;
    }

    for (const FString& Error : Errors)
    {
        FString Part;
        FString Message;
        SplitValidationError(Error, Part, Message);

        UE_LOG(
            LogPathLink,
            Error,
            TEXT("[PathLink][INVALID] Link=%s | Type=%s | Part=%s | Error=%s"),
            *GetName(),
            *TypeName,
            *Part,
            *Message);
    }

    return false;
}

void APathLink::ValidateInEditor()
{
    ValidateAndLog();
}

bool APathLink::LogValidationErrors() const
{
    TArray<FString> Errors;
    CollectValidationErrors(Errors, true);

    if (Errors.IsEmpty())
    {
        return true;
    }

    const FString TypeName = LinkTypeToString(LinkType);

    for (const FString& Error : Errors)
    {
        FString Part;
        FString Message;
        SplitValidationError(Error, Part, Message);

        UE_LOG(
            LogPathLink,
            Error,
            TEXT("[PathLink][INVALID] Link=%s | Type=%s | Part=%s | Error=%s"),
            *GetName(),
            *TypeName,
            *Part,
            *Message);
    }

    return false;
}

bool APathLink::HasDuplicatePlacementError(FString& OutDetails) const
{
    OutDetails.Reset();

    // ExitActor가 없거나 위치가 비정상인 경우에는 Duplicate 비교 자체를 수행할 수 없습니다.
    // 이런 오류는 기존 Validation에서 별도로 잡습니다.
    if (!IsValid(ExitActor))
    {
        return false;
    }

    const FVector ResolvedEntry = GetActorLocation();
    const FVector ResolvedExit = ResolveExitPoint(ExitActor, ExitOffset);

    if (ResolvedEntry.ContainsNaN() || ResolvedExit.ContainsNaN())
    {
        return false;
    }

    TArray<FString> DuplicateErrors;
    CollectDuplicatePlacementErrors(ResolvedEntry, ResolvedExit, DuplicateErrors);

    if (DuplicateErrors.IsEmpty())
    {
        return false;
    }

    OutDetails = FString::Join(DuplicateErrors, TEXT("\n"));
    return true;
}

void APathLink::CollectValidationErrors(TArray<FString>& OutErrors, const bool bIncludeDuplicatePlacement) const
{
    OutErrors.Reset();

    if (const UEnum* LinkTypeEnum = StaticEnum<EPathLinkType>())
    {
        if (!LinkTypeEnum->IsValidEnumValue(static_cast<int64>(LinkType)))
        {
            OutErrors.Add(TEXT("[LinkType] LinkType 값이 유효한 EPathLinkType 범위를 벗어났습니다."));
            return;
        }
    }

    // Entry는 별도 Actor가 아니라 PathLink 자기 자신입니다.
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        OutErrors.Add(TEXT("[World] PathLink가 유효한 World를 찾을 수 없습니다."));
        return;
    }

    if (GetActorLocation().ContainsNaN())
    {
        OutErrors.Add(TEXT("[EntryLocation] PathLink 자신의 위치에 NaN 또는 유효하지 않은 수치가 포함되어 있습니다."));
    }

    if (!IsValid(ExitActor))
    {
        // ExitActor가 없는 PathLink는 "이동 Link"가 아니라 위치 Marker로 취급합니다.
        // 다른 PathLink의 ExitActor로 참조할 수 있으며, Enabled가 true여도 Route 후보에는 들어가지 않습니다.
        return;
    }

    if (ExitActor == this)
    {
        OutErrors.Add(TEXT("[ExitActor] PathLink 자기 자신을 ExitActor로 지정할 수 없습니다."));
    }

    if (ExitActor->GetWorld() != World)
    {
        OutErrors.Add(TEXT("[ExitActor] ExitActor가 PathLink와 다른 World에 속해 있습니다."));
    }

    if (ExitOffset.ContainsNaN())
    {
        OutErrors.Add(TEXT("[ExitOffset] ExitOffset에 NaN 또는 유효하지 않은 수치가 포함되어 있습니다."));
    }

    // Type별 설정 검사
    switch (LinkType)
    {
    case EPathLinkType::Teleport:
        // Teleport도 다른 LinkType과 동일하게 ExitActor의 위치만 사용합니다.
        // PathLink는 Portal/ExitDirection 같은 특정 기믹 구현에 의존하지 않습니다.
        break;

    case EPathLinkType::JumpPad:
        // PathLink 자체가 JumpPad 진입점이므로 별도의 진입 Actor 참조를 받지 않습니다.
        if (TwoWay)
        {
            OutErrors.Add(TEXT("[TwoWay] JumpPad 타입은 단방향 Self -> Exit로 사용해야 합니다. TwoWay를 false로 설정하세요."));
        }
        break;

    case EPathLinkType::Drop:
        if (TwoWay)
        {
            OutErrors.Add(TEXT("[TwoWay] Drop 타입은 단방향 Self -> Exit로 사용해야 합니다. TwoWay를 false로 설정하세요."));
        }
        break;

    case EPathLinkType::Jump:
    default:
        break;
    }

    if (!OutErrors.IsEmpty())
    {
        return;
    }

    const FVector ResolvedEntry = GetActorLocation();
    const FVector ResolvedExit = ResolveExitPoint(ExitActor, ExitOffset);

    if (ResolvedExit.ContainsNaN())
    {
        OutErrors.Add(TEXT("[ExitLocation] 계산된 Exit 위치에 NaN 또는 유효하지 않은 수치가 포함되어 있습니다."));
        return;
    }

    if (ResolvedEntry.Equals(ResolvedExit, 1.0f))
    {
        OutErrors.Add(FString::Printf(
            TEXT("[EntryLocation/ExitLocation] PathLink Entry와 Exit 위치가 사실상 같습니다. Entry=(%s), Exit=(%s)"),
            *ResolvedEntry.ToCompactString(),
            *ResolvedExit.ToCompactString()));
        return;
    }

    if (LinkType == EPathLinkType::Drop && ResolvedEntry.Z <= ResolvedExit.Z + 1.0f)
    {
        OutErrors.Add(FString::Printf(
            TEXT("[DropDirection] Drop은 PathLink Entry가 Exit보다 높은 위치여야 합니다. EntryZ=%.2f, ExitZ=%.2f"),
            ResolvedEntry.Z,
            ResolvedExit.Z));
    }

    // PathLink Actor 자체가 Entry이므로 지면 배치 상태도 검사합니다.
    FHitResult GroundHit;
    if (!TraceGround(GroundHit))
    {
        OutErrors.Add(TEXT("[EntryGround] PathLink Entry 아래에서 지면을 찾을 수 없습니다. 배치 위치를 확인하세요."));
    }
    else
    {
        const float GroundGap = FMath::Abs(ResolvedEntry.Z - GroundHit.ImpactPoint.Z);
        if (GroundGap > PathLinkPlacement::GroundTolerance)
        {
            OutErrors.Add(FString::Printf(
                TEXT("[EntryGround] PathLink Entry가 지면에서 %.2fcm 떨어져 있습니다. Details의 'Attach To Ground' 버튼을 눌러 지면에 맞춰주세요."),
                GroundGap));
        }
    }

    // 일반 공개 Validation에서는 중복 배치까지 검사합니다.
    // Route Cache 구축 시에는 Subsystem이 전체 중복 검사를 한 번만 수행하므로 여기서는 생략할 수 있습니다.
    if (bIncludeDuplicatePlacement)
    {
        CollectDuplicatePlacementErrors(ResolvedEntry, ResolvedExit, OutErrors);

        if (!OutErrors.IsEmpty())
        {
            return;
        }
    }

}

void APathLink::CollectDuplicatePlacementErrors(
    const FVector& ResolvedEntry,
    const FVector& ResolvedExit,
    TArray<FString>& OutErrors) const
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    const double ToleranceSquared = FMath::Square(static_cast<double>(PathLinkPlacement::DuplicatePointTolerance));

    for (TActorIterator<APathLink> It(World); It; ++It)
    {
        const APathLink* Other = *It;
        if (!IsValid(Other) || Other == this || Other->LinkType != LinkType || !IsValid(Other->ExitActor))
        {
            continue;
        }

        const FVector OtherEntry = Other->GetActorLocation();
        const FVector OtherExit = Other->ResolveExitPoint(Other->ExitActor, Other->ExitOffset);

        if (OtherEntry.ContainsNaN() || OtherExit.ContainsNaN())
        {
            continue;
        }

        const bool SameForward =
            FVector::DistSquared(ResolvedEntry, OtherEntry) <= ToleranceSquared
            && FVector::DistSquared(ResolvedExit, OtherExit) <= ToleranceSquared;

        if (SameForward)
        {
            OutErrors.Add(FString::Printf(
                TEXT("[DuplicatePlacement] '%s'와 같은 타입/Entry/Exit 연결이 중복 배치되어 있습니다. 두 Link 중 불필요한 Link를 제거하세요."),
                *Other->GetName()));
            continue;
        }

        // 서로 반대 방향의 단방향 Link 두 개는 의도된 구성일 수 있으므로 허용합니다.
        // 단, 둘 중 하나라도 TwoWay이면 이미 반대 방향 Edge까지 포함하므로 중복으로 판단합니다.
        const bool SameReverse =
            FVector::DistSquared(ResolvedEntry, OtherExit) <= ToleranceSquared
            && FVector::DistSquared(ResolvedExit, OtherEntry) <= ToleranceSquared;

        if (SameReverse && (TwoWay || Other->TwoWay))
        {
            OutErrors.Add(FString::Printf(
                TEXT("[DuplicatePlacement] '%s'와 역방향 연결이 겹칩니다. TwoWay Link와 별도 역방향 Link를 동시에 배치하지 마세요."),
                *Other->GetName()));
        }
    }
}

bool APathLink::TraceGround(FHitResult& OutHit) const
{
    OutHit = FHitResult();

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return false;
    }

    const FVector CurrentLocation = GetActorLocation();
    const FVector TraceStart = CurrentLocation + FVector::UpVector * PathLinkPlacement::GroundTraceStartHeight;
    const FVector TraceEnd = CurrentLocation - FVector::UpVector * PathLinkPlacement::GroundTraceDistance;

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PathLinkAttachToGround), false, this);
    QueryParams.AddIgnoredActor(this);

    // 일반 지면/StaticMesh/Landscape가 기본적으로 Block하는 Visibility 채널을 사용합니다.
    // Trigger Volume 등에 잘못 Snap되는 것을 피하기 위해 ObjectType 전체 조회는 사용하지 않습니다.
    return World->LineTraceSingleByChannel(
        OutHit,
        TraceStart,
        TraceEnd,
        ECC_Visibility,
        QueryParams);
}

void APathLink::CollectNavigationErrors(TArray<FString>& OutErrors) const
{
    OutErrors.Reset();

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        OutErrors.Add(TEXT("[World] PathLink가 유효한 World를 찾을 수 없습니다."));
        return;
    }

    // Client는 AI/NavMesh Route 계산 주체가 아닙니다.
    // Client에 NavData가 없다는 이유로 정상 배치된 Link를 Invalid로 만들지 않습니다.
    if (World->GetNetMode() == NM_Client)
    {
        return;
    }

    // ExitActor가 없는 Marker 전용 PathLink는 자체 Navigation 검사가 필요하지 않습니다.
    // 이 Actor를 Exit로 참조하는 실제 Link가 자신의 Exit 위치를 Navigation에 투영해 검증합니다.
    if (!IsValid(ExitActor))
    {
        return;
    }

    if (!IsValidLink())
    {
        OutErrors.Add(TEXT("[Validation] Link 구조 Validation이 실패해 Navigation 사용 여부를 검사할 수 없습니다."));
        return;
    }

    UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World);
    if (!IsValid(NavSystem))
    {
        OutErrors.Add(TEXT("[NavigationSystem] 현재 World에서 NavigationSystem을 찾을 수 없습니다. NavMesh 설정을 확인하세요."));
        return;
    }

    const FVector ResolvedEntry = GetActorLocation();
    const FVector ResolvedExit = ResolveExitPoint(ExitActor, ExitOffset);

    if (!CanProjectToNavigation(ResolvedEntry))
    {
        OutErrors.Add(FString::Printf(
            TEXT("[EntryNavigation] PathLink Entry 위치를 NavMesh에 투영할 수 없습니다. Entry=(%s)"),
            *ResolvedEntry.ToCompactString()));
    }

    if (!CanProjectToNavigation(ResolvedExit))
    {
        OutErrors.Add(FString::Printf(
            TEXT("[ExitNavigation] Exit 위치를 NavMesh에 투영할 수 없습니다. Exit=(%s)"),
            *ResolvedExit.ToCompactString()));
    }
}

bool APathLink::CanUseForNavigation() const
{
    UWorld* World = GetWorld();
    if (!IsValid(World) || World->GetNetMode() == NM_Client)
    {
        return false;
    }

    if (!IsUsable())
    {
        return false;
    }

    TArray<FString> NavigationErrors;
    CollectNavigationErrors(NavigationErrors);
    return NavigationErrors.IsEmpty();
}

bool APathLink::CanProjectToNavigation(const FVector& WorldLocation) const
{
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
    const FVector ProjectionExtent(150.0f, 150.0f, 400.0f);

    return NavSystem->ProjectPointToNavigation(
        WorldLocation,
        ProjectedLocation,
        ProjectionExtent,
        static_cast<const FNavAgentProperties*>(nullptr),
        FSharedConstNavQueryFilter());
}

FVector APathLink::GetEntryLocation() const
{
    // 구조 변경 후 Entry는 항상 PathLink Actor 자기 자신입니다.
    return GetActorLocation();
}

FVector APathLink::GetExitLocation() const
{
    return IsValid(ExitActor)
        ? ResolveExitPoint(ExitActor, ExitOffset)
        : GetActorLocation();
}

bool APathLink::ResolveTravelLocations(
    const bool Reverse,
    FVector& OutEntryLocation,
    FVector& OutExitLocation,
    FText& OutFailureReason) const
{
    FString FailureReason;
    const bool Resolved = TryResolveTravelLocations(
        Reverse,
        OutEntryLocation,
        OutExitLocation,
        FailureReason);

    OutFailureReason = Resolved
        ? FText::GetEmpty()
        : FText::FromString(FailureReason);

    return Resolved;
}

bool APathLink::TryResolveTravelLocations(
    const bool Reverse,
    FVector& OutEntryLocation,
    FVector& OutExitLocation,
    FString& OutFailureReason) const
{
    OutEntryLocation = FVector::ZeroVector;
    OutExitLocation = FVector::ZeroVector;
    OutFailureReason.Reset();

    if (!IsValid(ExitActor))
    {
        OutFailureReason = TEXT("[ExitActor] ExitActor가 없는 PathLink는 Exit Marker 전용이며 이동 Link로 사용할 수 없습니다.");
        return false;
    }

    TArray<FString> ValidationErrors;
    CollectValidationErrors(ValidationErrors, true);
    if (!ValidationErrors.IsEmpty())
    {
        OutFailureReason = FString::Join(ValidationErrors, TEXT(" | "));
        return false;
    }

    if (Reverse && !TwoWay)
    {
        OutFailureReason = TEXT("[Reverse] TwoWay가 false이므로 역방향 이동을 사용할 수 없습니다.");
        return false;
    }

    const FVector ForwardEntry = GetActorLocation();
    const FVector ForwardExit = ResolveExitPoint(ExitActor, ExitOffset);

    // TwoWay는 같은 두 Endpoint를 내부적으로 뒤집어 사용합니다.
    OutEntryLocation = Reverse ? ForwardExit : ForwardEntry;
    OutExitLocation = Reverse ? ForwardEntry : ForwardExit;

    if (OutEntryLocation.ContainsNaN() || OutExitLocation.ContainsNaN())
    {
        OutFailureReason = TEXT("[TravelLocation] 계산된 이동 위치가 유효한 Vector가 아닙니다.");
        return false;
    }

    if (OutEntryLocation.Equals(OutExitLocation, 1.0f))
    {
        OutFailureReason = TEXT("[TravelLocation] 해석된 Entry와 Exit 위치가 같습니다.");
        return false;
    }

    return true;
}

double APathLink::GetTravelDistance(const bool Reverse) const
{
    FVector TravelEntry;
    FVector TravelExit;
    FString FailureReason;

    if (!TryResolveTravelLocations(Reverse, TravelEntry, TravelExit, FailureReason))
    {
        return 0.0;
    }

    // 순간이동은 실제 걷거나 날아가는 이동거리가 없으므로 0으로 계산합니다.
    if (LinkType == EPathLinkType::Teleport)
    {
        return 0.0;
    }

    return FVector::Distance(TravelEntry, TravelExit);
}

FVector APathLink::ResolveExitPoint(AActor* Actor, const FVector& LocalOffset) const
{
    if (!IsValid(Actor))
    {
        return FVector::ZeroVector;
    }

    // 모든 LinkType은 동일하게 ExitActor의 위치를 Exit 기준점으로 사용합니다.
    // Teleport도 Portal 클래스/ExitDirection 컴포넌트를 특별 취급하지 않습니다.
    return AddLocalOffset(Actor, Actor->GetActorLocation(), LocalOffset);
}

#if WITH_EDITOR
FColor APathLink::GetVisualColor() const
{
    // 색상은 LinkType의 의미 자체이므로 외부에서 변경할 수 없도록 코드에 고정합니다.
    switch (LinkType)
    {
    case EPathLinkType::Teleport:
        return FColor(170, 80, 255); // 보라색

    case EPathLinkType::JumpPad:
        return FColor(60, 220, 90); // 초록색

    case EPathLinkType::Jump:
        return FColor(255, 220, 40); // 노란색

    case EPathLinkType::Drop:
        return FColor(70, 140, 255); // 파란색

    default:
        return FColor::White;
    }
}

void APathLink::DrawEditorVisual() const
{
    if (!ShowVisual || !IsValid(ExitActor))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    // Entry는 PathLink Actor 자신의 위치입니다.
    // Exit은 타입별 Resolver를 사용해 실제 Route 계산 위치와 동일하게 표시합니다.
    const FVector EntryVisual = GetActorLocation();
    const FVector ExitVisual = ResolveExitPoint(ExitActor, ExitOffset);

    if (EntryVisual.ContainsNaN() || ExitVisual.ContainsNaN())
    {
        return;
    }

    const FVector Direction = (ExitVisual - EntryVisual).GetSafeNormal();
    if (Direction.IsNearlyZero())
    {
        return;
    }

    const FColor Color = GetVisualColor();

    DrawDebugLine(
        World,
        EntryVisual,
        ExitVisual,
        Color,
        false,
        0.0f,
        0,
        Enabled ? 3.0f : 1.0f);

    // 단방향은 Exit 쪽에만 화살표를 표시합니다.
    DrawArrowHead(ExitVisual, Direction, Color);

    // TwoWay이면 Entry 쪽에도 반대 방향 화살표를 추가합니다.
    if (TwoWay)
    {
        DrawArrowHead(EntryVisual, -Direction, Color);
    }
}

void APathLink::DrawArrowHead(
    const FVector& Tip,
    const FVector& Direction,
    const FColor& Color) const
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    constexpr float ArrowLength = 70.0f;
    constexpr float ArrowHalfWidth = 28.0f;

    FVector ReferenceAxis = FVector::UpVector;
    if (FMath::Abs(FVector::DotProduct(Direction, ReferenceAxis)) > 0.95f)
    {
        ReferenceAxis = FVector::RightVector;
    }

    const FVector Side = FVector::CrossProduct(Direction, ReferenceAxis).GetSafeNormal();
    const FVector Base = Tip - Direction * ArrowLength;
    const FVector Left = Base + Side * ArrowHalfWidth;
    const FVector Right = Base - Side * ArrowHalfWidth;

    DrawDebugLine(World, Tip, Left, Color, false, 0.0f, 0, Enabled ? 3.0f : 1.0f);
    DrawDebugLine(World, Tip, Right, Color, false, 0.0f, 0, Enabled ? 3.0f : 1.0f);
}
#endif
