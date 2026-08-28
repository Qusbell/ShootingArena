#include "Providers/PathLinkValidationProvider.h"

#include "Integration/GameplayValidatorPathLinkBridge.h"
#include "Utils/GameplayValidationUtils.h"
#include "UObject/UnrealType.h"

const FName FPathLinkValidationProvider::ProviderId(TEXT("PathLink"));
const FName FPathLinkValidationProvider::DefaultSlotId(TEXT("Default"));

FText FPathLinkValidationProvider::GetDisplayName() const
{
    return NSLOCTEXT("GameplayValidator", "PathLinkProvider", "PathLink");
}

void FPathLinkValidationProvider::GetTargetSlots(TArray<FGameplayValidationTargetSlot>& OutSlots) const
{
    FGameplayValidationTargetSlot& Slot = OutSlots.AddDefaulted_GetRef();
    Slot.SlotId = DefaultSlotId;
    Slot.DisplayName = NSLOCTEXT("GameplayValidator", "PathLinkClass", "PathLink");
}

bool FPathLinkValidationProvider::HasFallbackPropertyContract(UClass* Class)
{
    if (!IsValid(Class))
    {
        return false;
    }

    return FindFProperty<FObjectPropertyBase>(Class, TEXT("ExitActor")) != nullptr
        && FindFProperty<FBoolProperty>(Class, TEXT("Enabled")) != nullptr;
}

bool FPathLinkValidationProvider::ValidateTargetClass(
    const FName SlotId,
    UClass* TargetClass,
    FText& OutReason) const
{
    if (!IGameplayValidationProvider::ValidateTargetClass(SlotId, TargetClass, OutReason))
    {
        return false;
    }

    // Native Bridge가 등록되어 있다면 실제 APathLink 상속 여부를 직접 확인합니다.
    if (FGameplayValidatorPathLinkBridge::IsRegistered())
    {
        if (FGameplayValidatorPathLinkBridge::CanHandleClass(TargetClass))
        {
            OutReason = FText::GetEmpty();
            return true;
        }

        OutReason = NSLOCTEXT(
            "GameplayValidator",
            "PathLinkWrongNativeClass",
            "등록한 Blueprint가 현재 프로젝트의 APathLink를 상속하지 않습니다.");
        return false;
    }

    // Bridge가 아직 등록되지 않은 특수 상황에서도 Class Picker 자체가 막히지 않도록
    // 최소한의 읽기 전용 Property 계약만 확인합니다.
    if (HasFallbackPropertyContract(TargetClass))
    {
        OutReason = FText::GetEmpty();
        return true;
    }

    OutReason = NSLOCTEXT(
        "GameplayValidator",
        "PathLinkFallbackContractMismatch",
        "PathLink Blueprint에서 ExitActor / Enabled 속성을 찾을 수 없습니다.");
    return false;
}

AActor* FPathLinkValidationProvider::ReadExitActorProperty(const AActor* Link)
{
    if (!IsValid(Link))
    {
        return nullptr;
    }

    const FObjectPropertyBase* Property =
        FindFProperty<FObjectPropertyBase>(Link->GetClass(), TEXT("ExitActor"));

    return Property
        ? Cast<AActor>(Property->GetObjectPropertyValue_InContainer(Link))
        : nullptr;
}

bool FPathLinkValidationProvider::ReadEnabledProperty(const AActor* Link, bool& OutEnabled)
{
    OutEnabled = false;

    if (!IsValid(Link))
    {
        return false;
    }

    const FBoolProperty* Property =
        FindFProperty<FBoolProperty>(Link->GetClass(), TEXT("Enabled"));

    if (!Property)
    {
        return false;
    }

    OutEnabled = Property->GetPropertyValue_InContainer(Link);
    return true;
}

void FPathLinkValidationProvider::Prepare(const FGameplayValidationContext& Context)
{
    Links.Reset();
    Markers.Reset();
    ReferencedExitActors.Reset();

    RouteLinkCount = 0;
    MarkerCount = 0;
    DisabledCount = 0;
    UnusedMarkerCount = 0;

    for (const TWeakObjectPtr<AActor>& WeakActor : Context.GetActors(ProviderId, DefaultSlotId))
    {
        AActor* Link = WeakActor.Get();
        if (!IsValid(Link))
        {
            continue;
        }

        Links.Add(Link);

        AActor* ExitActor = nullptr;
        const bool bNativeExitRead =
            FGameplayValidatorPathLinkBridge::TryGetExitActor(Link, ExitActor);

        if (!bNativeExitRead)
        {
            // Native Bridge가 없을 때만 단순 Property Read로 분류합니다.
            ExitActor = ReadExitActorProperty(Link);
        }

        if (!IsValid(ExitActor))
        {
            // 현재 APathLink의 공식 의미: ExitActor 없음 = 다른 Link의 Exit Marker.
            Markers.Add(Link);
            ++MarkerCount;
            continue;
        }

        ReferencedExitActors.Add(ExitActor);

        bool bEnabled = false;
        const bool bNativeEnabledRead =
            FGameplayValidatorPathLinkBridge::TryIsEnabled(Link, bEnabled);

        const bool bEnabledRead = bNativeEnabledRead
            || ReadEnabledProperty(Link, bEnabled);

        if (!bEnabledRead)
        {
            // 값을 읽지 못한 경우 Invalid/Error로 추측하지 않습니다.
            // false positive를 막기 위해 Route Link로만 집계합니다.
            ++RouteLinkCount;
            continue;
        }

        if (bEnabled)
        {
            ++RouteLinkCount;
        }
        else
        {
            ++DisabledCount;
        }
    }
}

void FPathLinkValidationProvider::Validate(
    const FGameplayValidationContext& Context,
    TArray<FGameplayValidationIssue>& OutIssues)
{
    // 중요한 정책:
    // Native Bridge로 실제 APathLink::ValidateLink를 직접 호출할 수 있을 때만 Error를 생성합니다.
    // Bridge가 없거나 호출할 수 없는 상태를 Gameplay Error로 취급하지 않습니다.
    if (!FGameplayValidatorPathLinkBridge::IsRegistered())
    {
        return;
    }

    for (const TWeakObjectPtr<AActor>& WeakLink : Links)
    {
        AActor* Link = WeakLink.Get();
        if (!IsValid(Link))
        {
            continue;
        }

        AActor* ExitActor = nullptr;
        if (!FGameplayValidatorPathLinkBridge::TryGetExitActor(Link, ExitActor))
        {
            // 연결 자체의 문제는 Error로 만들지 않습니다.
            continue;
        }

        // ExitActor가 없는 PathLink는 공식적인 Exit Marker이며 정상입니다.
        if (!IsValid(ExitActor))
        {
            continue;
        }

        bool bValid = true;
        FText FailureReason;
        if (!FGameplayValidatorPathLinkBridge::TryValidateLink(Link, bValid, FailureReason))
        {
            // Bridge 호출 실패 역시 가짜 Gameplay Error를 만들지 않고 건너뜁니다.
            continue;
        }

        if (bValid)
        {
            continue;
        }

        // 실제 APathLink::ValidateLink가 false를 반환한 경우에만 Error가 생성됩니다.
        const FText Message = FailureReason.IsEmpty()
            ? NSLOCTEXT(
                "GameplayValidator",
                "PathLinkNativeValidationFailed",
                "PathLink 자체 Validation에 실패했습니다. Details의 'Validate Path Link'로 상태를 확인하세요.")
            : FailureReason;

        FGameplayValidationUtils::AddIssue(
            OutIssues,
            TEXT("PATHLINK_001"),
            ProviderId,
            DefaultSlotId,
            EGameplayValidationSeverity::Error,
            Link,
            Message,
            NSLOCTEXT(
                "GameplayValidator",
                "PathLinkInvalidFix",
                "오류 내용에 표시된 PathLink 설정을 수정한 뒤 다시 검사하세요."),
            ExitActor);
    }
}

void FPathLinkValidationProvider::Finalize(
    const FGameplayValidationContext& Context,
    TArray<FGameplayValidationIssue>& OutIssues)
{
    // Marker마다 모든 Link를 재탐색하지 않고 Prepare에서 만든 Set만 조회합니다.
    for (const TWeakObjectPtr<AActor>& WeakMarker : Markers)
    {
        AActor* Marker = WeakMarker.Get();
        if (!IsValid(Marker))
        {
            continue;
        }

        if (ReferencedExitActors.Contains(Marker))
        {
            continue;
        }

        ++UnusedMarkerCount;

        FGameplayValidationUtils::AddIssue(
            OutIssues,
            TEXT("PATHLINK_UNUSED_MARKER"),
            ProviderId,
            DefaultSlotId,
            EGameplayValidationSeverity::Warning,
            Marker,
            NSLOCTEXT(
                "GameplayValidator",
                "UnusedPathLinkMarker",
                "Exit Marker로 배치되어 있지만 현재 어떤 PathLink에서도 사용하지 않습니다."),
            NSLOCTEXT(
                "GameplayValidator",
                "UnusedPathLinkMarkerFix",
                "추후 사용할 Marker인지 확인하고, 필요하지 않다면 레벨에서 제거하세요."));
    }
}

void FPathLinkValidationProvider::GetSummaryMetrics(TArray<FGameplayValidationMetric>& OutMetrics) const
{
    auto AddMetric = [&OutMetrics](
        const FName Id,
        const FText& Name,
        const int32 Value,
        const int32 SortOrder)
    {
        FGameplayValidationMetric& Metric = OutMetrics.AddDefaulted_GetRef();
        Metric.ProviderId = ProviderId;
        Metric.MetricId = Id;
        Metric.DisplayName = Name;
        Metric.Value = Value;
        Metric.SortOrder = SortOrder;
    };

    AddMetric(
        TEXT("RouteLinks"),
        NSLOCTEXT("GameplayValidator", "MetricRouteLinks", "Route Link"),
        RouteLinkCount,
        0);

    AddMetric(
        TEXT("Markers"),
        NSLOCTEXT("GameplayValidator", "MetricMarkers", "Exit Marker"),
        MarkerCount,
        1);

    AddMetric(
        TEXT("Disabled"),
        NSLOCTEXT("GameplayValidator", "MetricDisabled", "Disabled Link"),
        DisabledCount,
        2);

    AddMetric(
        TEXT("UnusedMarkers"),
        NSLOCTEXT("GameplayValidator", "MetricUnusedMarkers", "Unused Marker"),
        UnusedMarkerCount,
        3);
}
