#include "AI/PathLink/PathLink.h"

#include "AI/PathLink/PathLinkSubsystem.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathLink, Log, All);

namespace PathLinkComponentNames
{
    static const FName PortalTrigger(TEXT("PortalTrigger"));
    static const FName ExitDirection(TEXT("ExitDirection"));
    static const FName JumpPadTrigger(TEXT("JumpPadTrigger"));
}

namespace
{
    /**
     * 기존 BP를 수정하지 않기 위해 Actor가 가진 SceneComponent를 이름으로 조회합니다.
     * Blueprint가 생성한 Component 이름에 접미사가 붙는 경우를 고려해 Contains도 함께 검사합니다.
     */
    USceneComponent* FindSceneComponentByName(const AActor* Actor, const FName PreferredName)
    {
        if (!IsValid(Actor))
        {
            return nullptr;
        }

        TInlineComponentArray<USceneComponent*> Components;
        Actor->GetComponents(Components);

        for (USceneComponent* Component : Components)
        {
            if (IsValid(Component) && Component->GetFName() == PreferredName)
            {
                return Component;
            }
        }

        const FString PreferredString = PreferredName.ToString();
        for (USceneComponent* Component : Components)
        {
            if (IsValid(Component) && Component->GetName().Contains(PreferredString))
            {
                return Component;
            }
        }

        return nullptr;
    }

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

bool APathLink::IsValidLink() const
{
    TArray<FString> Errors;
    CollectValidationErrors(Errors);
    return Errors.IsEmpty();
}

bool APathLink::ValidateLink(FText& OutFailureReason) const
{
    TArray<FString> Errors;
    CollectValidationErrors(Errors);

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
    CollectValidationErrors(Errors);

    const FString TypeName = LinkTypeToString(LinkType);

    if (Errors.IsEmpty())
    {
        UE_LOG(
            LogPathLink,
            Log,
            TEXT("[PathLink][VALID] Link=%s | Type=%s | EntryActor=%s | ExitActor=%s | TwoWay=%s | Enabled=%s"),
            *GetName(),
            *TypeName,
            *GetNameSafe(EntryActor.Get()),
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

bool APathLink::LogValidationErrors() const
{
    TArray<FString> Errors;
    CollectValidationErrors(Errors);

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

void APathLink::CollectValidationErrors(TArray<FString>& OutErrors) const
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

    // 1) 공통 Actor 설정 검사
    if (!IsValid(EntryActor))
    {
        OutErrors.Add(TEXT("[EntryActor] EntryActor가 지정되지 않았거나 유효하지 않습니다."));
    }

    if (!IsValid(ExitActor))
    {
        OutErrors.Add(TEXT("[ExitActor] ExitActor가 지정되지 않았거나 유효하지 않습니다."));
    }

    if (!OutErrors.IsEmpty())
    {
        // Actor가 없는 상태에서는 아래 검사가 의미가 없으므로 여기서 종료합니다.
        return;
    }

    if (EntryActor == this)
    {
        OutErrors.Add(TEXT("[EntryActor] PathLink 자기 자신을 EntryActor로 지정할 수 없습니다."));
    }

    if (ExitActor == this)
    {
        OutErrors.Add(TEXT("[ExitActor] PathLink 자기 자신을 ExitActor로 지정할 수 없습니다."));
    }

    if (EntryActor == ExitActor)
    {
        OutErrors.Add(TEXT("[EntryActor/ExitActor] EntryActor와 ExitActor가 같은 Actor입니다. 서로 다른 Actor를 지정해야 합니다."));
    }

    if (EntryActor->GetWorld() != GetWorld())
    {
        OutErrors.Add(TEXT("[EntryActor] EntryActor가 PathLink와 다른 World에 속해 있습니다."));
    }

    if (ExitActor->GetWorld() != GetWorld())
    {
        OutErrors.Add(TEXT("[ExitActor] ExitActor가 PathLink와 다른 World에 속해 있습니다."));
    }

    if (EntryOffset.ContainsNaN())
    {
        OutErrors.Add(TEXT("[EntryOffset] EntryOffset에 NaN 또는 유효하지 않은 수치가 포함되어 있습니다."));
    }

    if (ExitOffset.ContainsNaN())
    {
        OutErrors.Add(TEXT("[ExitOffset] ExitOffset에 NaN 또는 유효하지 않은 수치가 포함되어 있습니다."));
    }

    // 2) Type별 기존 기믹 구조 검사
    switch (LinkType)
    {
    case EPathLinkType::Teleport:
    {
        // 정방향은 Entry Portal의 PortalTrigger -> Exit Portal의 ExitDirection입니다.
        if (!FindSceneComponentByName(EntryActor, PathLinkComponentNames::PortalTrigger))
        {
            OutErrors.Add(FString::Printf(
                TEXT("[EntryActor.PortalTrigger] Teleport EntryActor '%s'에서 PortalTrigger 컴포넌트를 찾을 수 없습니다."),
                *GetNameSafe(EntryActor.Get())));
        }

        if (!FindSceneComponentByName(ExitActor, PathLinkComponentNames::ExitDirection))
        {
            OutErrors.Add(FString::Printf(
                TEXT("[ExitActor.ExitDirection] Teleport ExitActor '%s'에서 ExitDirection 컴포넌트를 찾을 수 없습니다."),
                *GetNameSafe(ExitActor.Get())));
        }

        // TwoWay이면 역방향도 실제 기믹 구조가 성립해야 합니다.
        if (TwoWay)
        {
            if (!FindSceneComponentByName(ExitActor, PathLinkComponentNames::PortalTrigger))
            {
                OutErrors.Add(FString::Printf(
                    TEXT("[TwoWay.ExitActor.PortalTrigger] TwoWay 역방향 진입을 위해 ExitActor '%s'에도 PortalTrigger가 필요합니다."),
                    *GetNameSafe(ExitActor.Get())));
            }

            if (!FindSceneComponentByName(EntryActor, PathLinkComponentNames::ExitDirection))
            {
                OutErrors.Add(FString::Printf(
                    TEXT("[TwoWay.EntryActor.ExitDirection] TwoWay 역방향 출구를 위해 EntryActor '%s'에도 ExitDirection이 필요합니다."),
                    *GetNameSafe(EntryActor.Get())));
            }
        }
        break;
    }

    case EPathLinkType::JumpPad:
    {
        if (!FindSceneComponentByName(EntryActor, PathLinkComponentNames::JumpPadTrigger))
        {
            OutErrors.Add(FString::Printf(
                TEXT("[EntryActor.JumpPadTrigger] JumpPad EntryActor '%s'에서 JumpPadTrigger 컴포넌트를 찾을 수 없습니다."),
                *GetNameSafe(EntryActor.Get())));
        }

        // 현재 구조는 EntryActor=JumpPad, ExitActor=TargetPoint이므로 하나의 Link를 역방향으로 사용할 수 없습니다.
        if (TwoWay)
        {
            OutErrors.Add(TEXT("[TwoWay] 현재 JumpPad Link 구조에서는 TwoWay를 지원하지 않습니다. 역방향 점프패드가 필요하면 별도의 PathLink를 추가하세요."));
        }
        break;
    }

    case EPathLinkType::Drop:
        // Drop은 의미상 Entry -> Exit 단방향 이동입니다.
        if (TwoWay)
        {
            OutErrors.Add(TEXT("[TwoWay] Drop 타입은 단방향 Entry -> Exit로 사용해야 합니다. TwoWay를 false로 설정하세요."));
        }
        break;

    case EPathLinkType::Jump:
    default:
        // Jump는 별도 기믹 Component 없이 위치 Actor만으로 사용할 수 있으며 TwoWay도 허용합니다.
        break;
    }

    // 타입 검사에서 이미 실패했다면 잘못된 Component fallback 위치로 NavMesh 검사까지 이어가지 않습니다.
    if (!OutErrors.IsEmpty())
    {
        return;
    }

    // 3) 실제 해석 위치 검사
    const FVector ResolvedEntry = ResolveEntryPoint(EntryActor, EntryOffset);
    const FVector ResolvedExit = ResolveExitPoint(ExitActor, ExitOffset);

    if (ResolvedEntry.ContainsNaN())
    {
        OutErrors.Add(TEXT("[EntryLocation] 계산된 Entry 위치에 NaN 또는 유효하지 않은 수치가 포함되어 있습니다."));
    }

    if (ResolvedExit.ContainsNaN())
    {
        OutErrors.Add(TEXT("[ExitLocation] 계산된 Exit 위치에 NaN 또는 유효하지 않은 수치가 포함되어 있습니다."));
    }

    if (!OutErrors.IsEmpty())
    {
        return;
    }

    if (ResolvedEntry.Equals(ResolvedExit, 1.0f))
    {
        OutErrors.Add(FString::Printf(
            TEXT("[EntryLocation/ExitLocation] 계산된 Entry와 Exit 위치가 사실상 같습니다. Entry=(%s), Exit=(%s)"),
            *ResolvedEntry.ToCompactString(),
            *ResolvedExit.ToCompactString()));
        return;
    }

    if (LinkType == EPathLinkType::Drop && ResolvedEntry.Z <= ResolvedExit.Z + 1.0f)
    {
        OutErrors.Add(FString::Printf(
            TEXT("[DropDirection] Drop은 Entry가 Exit보다 높은 위치여야 합니다. EntryZ=%.2f, ExitZ=%.2f"),
            ResolvedEntry.Z,
            ResolvedExit.Z));
    }

    if (!OutErrors.IsEmpty())
    {
        return;
    }

    // 4) NavMesh 연결 가능성 검사
    // Route는 일반 NavMesh 구간과 Link를 결합하므로 양쪽 Endpoint가 NavMesh에 투영 가능해야 합니다.
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        OutErrors.Add(TEXT("[World] PathLink가 유효한 World를 찾을 수 없습니다."));
        return;
    }

    if (!IsValid(UNavigationSystemV1::GetCurrent(World)))
    {
        OutErrors.Add(TEXT("[NavigationSystem] 현재 World에서 NavigationSystem을 찾을 수 없습니다. NavMesh 설정을 확인하세요."));
        return;
    }

    if (!CanProjectToNavigation(ResolvedEntry))
    {
        OutErrors.Add(FString::Printf(
            TEXT("[EntryNavigation] Entry 위치를 NavMesh에 투영할 수 없습니다. Entry=(%s)"),
            *ResolvedEntry.ToCompactString()));
    }

    if (!CanProjectToNavigation(ResolvedExit))
    {
        OutErrors.Add(FString::Printf(
            TEXT("[ExitNavigation] Exit 위치를 NavMesh에 투영할 수 없습니다. Exit=(%s)"),
            *ResolvedExit.ToCompactString()));
    }
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
    FVector EntryLocation;
    FVector ExitLocation;
    FString FailureReason;

    if (TryResolveTravelLocations(false, EntryLocation, ExitLocation, FailureReason))
    {
        return EntryLocation;
    }

    return IsValid(EntryActor)
        ? AddLocalOffset(EntryActor, EntryActor->GetActorLocation(), EntryOffset)
        : GetActorLocation();
}

FVector APathLink::GetExitLocation() const
{
    FVector EntryLocation;
    FVector ExitLocation;
    FString FailureReason;

    if (TryResolveTravelLocations(false, EntryLocation, ExitLocation, FailureReason))
    {
        return ExitLocation;
    }

    return IsValid(ExitActor)
        ? AddLocalOffset(ExitActor, ExitActor->GetActorLocation(), ExitOffset)
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

    TArray<FString> ValidationErrors;
    CollectValidationErrors(ValidationErrors);
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

    AActor* TravelEntryActor = Reverse ? ExitActor.Get() : EntryActor.Get();
    AActor* TravelExitActor = Reverse ? EntryActor.Get() : ExitActor.Get();

    const FVector TravelEntryOffset = Reverse ? ExitOffset : EntryOffset;
    const FVector TravelExitOffset = Reverse ? EntryOffset : ExitOffset;

    OutEntryLocation = ResolveEntryPoint(TravelEntryActor, TravelEntryOffset);
    OutExitLocation = ResolveExitPoint(TravelExitActor, TravelExitOffset);

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

FVector APathLink::ResolveEntryPoint(AActor* Actor, const FVector& LocalOffset) const
{
    if (!IsValid(Actor))
    {
        return FVector::ZeroVector;
    }

    FVector BaseLocation = Actor->GetActorLocation();

    switch (LinkType)
    {
    case EPathLinkType::Teleport:
        // 기존 BPA_YJS_Portal_TwoWay는 PortalTrigger 진입 시 순간이동을 실행합니다.
        if (const USceneComponent* PortalTrigger = FindSceneComponentByName(
            Actor,
            PathLinkComponentNames::PortalTrigger))
        {
            BaseLocation = PortalTrigger->GetComponentLocation();
        }
        break;

    case EPathLinkType::JumpPad:
        // 기존 BPA_YJS_Direction_JumpPad는 JumpPadTrigger Overlap으로 실행됩니다.
        if (const USceneComponent* JumpPadTrigger = FindSceneComponentByName(
            Actor,
            PathLinkComponentNames::JumpPadTrigger))
        {
            BaseLocation = JumpPadTrigger->GetComponentLocation();
        }
        break;

    case EPathLinkType::Jump:
    case EPathLinkType::Drop:
    default:
        // Jump/Drop은 별도 기믹 Actor가 없을 수 있으므로 지정한 Actor 위치를 그대로 기준으로 사용합니다.
        break;
    }

    return AddLocalOffset(Actor, BaseLocation, LocalOffset);
}

FVector APathLink::ResolveExitPoint(AActor* Actor, const FVector& LocalOffset) const
{
    if (!IsValid(Actor))
    {
        return FVector::ZeroVector;
    }

    FVector BaseLocation = Actor->GetActorLocation();

    if (LinkType == EPathLinkType::Teleport)
    {
        // 기존 Portal은 TargetPortal의 ExitDirection 위치로 Character를 이동시킵니다.
        if (const USceneComponent* ExitDirection = FindSceneComponentByName(
            Actor,
            PathLinkComponentNames::ExitDirection))
        {
            BaseLocation = ExitDirection->GetComponentLocation();
        }
    }

    // JumpPad ExitActor는 기존 JumpPad의 Target Point Actor를 지정하므로 ActorLocation을 그대로 사용합니다.
    return AddLocalOffset(Actor, BaseLocation, LocalOffset);
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
    if (!ShowVisual || !IsValid(EntryActor) || !IsValid(ExitActor))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    // Visual은 실제 Route 계산에서 사용하는 위치와 동일한 Resolver를 사용합니다.
    // 따라서 PortalTrigger / ExitDirection / JumpPadTrigger 보정 결과를 그대로 확인할 수 있습니다.
    FVector EntryVisual = ResolveEntryPoint(EntryActor, EntryOffset);
    FVector ExitVisual = ResolveExitPoint(ExitActor, ExitOffset);

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

    // 매 Editor Frame 다시 그리기 때문에 참조 Actor를 이동하면 선/화살표도 즉시 따라갑니다.
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
