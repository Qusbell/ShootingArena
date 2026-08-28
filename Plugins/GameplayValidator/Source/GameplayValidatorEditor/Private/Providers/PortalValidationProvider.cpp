#include "Providers/PortalValidationProvider.h"

#include "Utils/GameplayValidationReflectionUtils.h"
#include "Utils/GameplayValidationUtils.h"

const FName FPortalValidationProvider::ProviderId(TEXT("Portal"));
const FName FPortalValidationProvider::TwoWaySlotId(TEXT("TwoWay"));
const FName FPortalValidationProvider::OneWaySlotId(TEXT("OneWay"));

namespace PortalBindings
{
    // 기획자가 레벨에 Portal을 배치한 뒤 직접 지정하는 연결 대상만 검사합니다.
    // Trigger / Collision / ExitDirection 등 BP 내부 구성은 완성된 Blueprint 자체의 책임으로 봅니다.
    const TArray<FName> TargetPortalNames = { TEXT("TargetPortal"), TEXT("Target Portal") };
}

FText FPortalValidationProvider::GetDisplayName() const
{
    return NSLOCTEXT("GameplayValidator", "PortalProvider", "Portal");
}

void FPortalValidationProvider::GetTargetSlots(TArray<FGameplayValidationTargetSlot>& OutSlots) const
{
    FGameplayValidationTargetSlot& TwoWay = OutSlots.AddDefaulted_GetRef();
    TwoWay.SlotId = TwoWaySlotId;
    TwoWay.DisplayName = NSLOCTEXT("GameplayValidator", "TwoWayPortalClass", "TwoWay Portal");

    FGameplayValidationTargetSlot& OneWay = OutSlots.AddDefaulted_GetRef();
    OneWay.SlotId = OneWaySlotId;
    OneWay.DisplayName = NSLOCTEXT("GameplayValidator", "OneWayPortalClass", "OneWay Portal");
}

bool FPortalValidationProvider::ValidateTargetClass(
    const FName SlotId,
    UClass* TargetClass,
    FText& OutReason) const
{
    if (!IGameplayValidationProvider::ValidateTargetClass(SlotId, TargetClass, OutReason))
    {
        return false;
    }

    // Portal BP 내부의 Trigger / Collision / ExitDirection 구성은 검사하지 않습니다.
    // 기획자가 레벨 인스턴스에서 직접 지정해야 하는 TargetPortal만 Contract로 요구합니다.
    if (!FGameplayValidationReflectionUtils::HasProperty(TargetClass, PortalBindings::TargetPortalNames))
    {
        OutReason = NSLOCTEXT(
            "GameplayValidator",
            "PortalClassContractMismatch",
            "Portal 슬롯에는 TargetPortal을 가진 Portal Blueprint를 등록해야 합니다.");
        return false;
    }

    OutReason = FText::GetEmpty();
    return true;
}

void FPortalValidationProvider::Validate(
    const FGameplayValidationContext& Context,
    TArray<FGameplayValidationIssue>& OutIssues)
{
    for (const TWeakObjectPtr<AActor>& WeakActor : Context.GetActors(ProviderId, TwoWaySlotId))
    {
        if (AActor* Actor = WeakActor.Get(); IsValid(Actor))
        {
            ValidatePortalActor(TwoWaySlotId, Actor, OutIssues);
        }
    }

    for (const TWeakObjectPtr<AActor>& WeakActor : Context.GetActors(ProviderId, OneWaySlotId))
    {
        if (AActor* Actor = WeakActor.Get(); IsValid(Actor))
        {
            ValidatePortalActor(OneWaySlotId, Actor, OutIssues);
        }
    }
}

void FPortalValidationProvider::ValidatePortalActor(
    const FName SlotId,
    AActor* Portal,
    TArray<FGameplayValidationIssue>& OutIssues) const
{
    AActor* TargetPortal = FGameplayValidationReflectionUtils::GetActorProperty(
        Portal,
        PortalBindings::TargetPortalNames);

    // PORTAL_001: 기획자가 반드시 연결해야 하는 TargetPortal 누락
    if (!IsValid(TargetPortal))
    {
        FGameplayValidationUtils::AddIssue(
            OutIssues,
            TEXT("PORTAL_001"),
            ProviderId,
            SlotId,
            EGameplayValidationSeverity::Error,
            Portal,
            NSLOCTEXT("GameplayValidator", "PortalMissingTarget", "TargetPortal이 지정되지 않았습니다."),
            NSLOCTEXT("GameplayValidator", "PortalMissingTargetFix", "이 Portal이 연결될 TargetPortal을 지정하세요."));
        return;
    }

    // PORTAL_002: 자기 자신을 Target으로 잘못 연결한 경우
    if (TargetPortal == Portal)
    {
        FGameplayValidationUtils::AddIssue(
            OutIssues,
            TEXT("PORTAL_002"),
            ProviderId,
            SlotId,
            EGameplayValidationSeverity::Error,
            Portal,
            NSLOCTEXT("GameplayValidator", "PortalSelfTarget", "Portal 자기 자신이 TargetPortal로 지정되어 있습니다."),
            NSLOCTEXT("GameplayValidator", "PortalSelfTargetFix", "다른 Portal Actor를 TargetPortal로 지정하세요."),
            TargetPortal);
    }
}
