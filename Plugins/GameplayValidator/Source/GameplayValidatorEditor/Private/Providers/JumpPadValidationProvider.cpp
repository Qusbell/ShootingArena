#include "Providers/JumpPadValidationProvider.h"

#include "NavigationSystem.h"
#include "Utils/GameplayValidationReflectionUtils.h"
#include "Utils/GameplayValidationUtils.h"

const FName FJumpPadValidationProvider::ProviderId(TEXT("JumpPad"));
const FName FJumpPadValidationProvider::DefaultSlotId(TEXT("Default"));

namespace JumpPadBindings
{
    // 기획자가 레벨에 배치한 뒤 직접 지정하는 값만 Contract로 검사합니다.
    const TArray<FName> TargetPointNames = { TEXT("Target Point"), TEXT("TargetPoint") };

    // 프로젝트의 기존 PathLink Navigation 투영 범위와 동일한 크기를 사용합니다.
    // Actor Pivot이 NavMesh에서 약간 떠 있어도 오탐이 나지 않도록 여유 범위를 둡니다.
    const FVector NavigationProjectionExtent(150.0f, 150.0f, 400.0f);

    /**
     * 지정 위치 주변에서 실제 NavMesh 위치를 찾을 수 있는지만 검사합니다.
     * NavigationSystem 자체가 없는 경우에는 레벨 전체 Navigation 설정 문제일 수 있으므로
     * JumpPad마다 같은 Warning을 반복하지 않고 이 검사는 건너뜁니다.
     */
    bool CanProjectToNavigation(UWorld* World, const FVector& WorldLocation)
    {
        if (!IsValid(World))
        {
            return true;
        }

        UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World);
        if (!IsValid(NavSystem))
        {
            return true;
        }

        FNavLocation ProjectedLocation;
        return NavSystem->ProjectPointToNavigation(
            WorldLocation,
            ProjectedLocation,
            NavigationProjectionExtent,
            static_cast<const FNavAgentProperties*>(nullptr),
            FSharedConstNavQueryFilter());
    }
}

FText FJumpPadValidationProvider::GetDisplayName() const
{
    return NSLOCTEXT("GameplayValidator", "JumpPadProvider", "JumpPad");
}

void FJumpPadValidationProvider::GetTargetSlots(TArray<FGameplayValidationTargetSlot>& OutSlots) const
{
    FGameplayValidationTargetSlot& Slot = OutSlots.AddDefaulted_GetRef();
    Slot.SlotId = DefaultSlotId;
    Slot.DisplayName = NSLOCTEXT("GameplayValidator", "JumpPadClass", "JumpPad");
}

bool FJumpPadValidationProvider::ValidateTargetClass(
    const FName SlotId,
    UClass* TargetClass,
    FText& OutReason) const
{
    if (!IGameplayValidationProvider::ValidateTargetClass(SlotId, TargetClass, OutReason))
    {
        return false;
    }

    // JumpPad BP 내부의 Trigger/Collision/Event 구성은 완성된 Blueprint 자체의 책임입니다.
    // Validator는 기획자가 레벨 인스턴스에서 직접 지정하는 Target Point만 요구합니다.
    if (!FGameplayValidationReflectionUtils::HasProperty(TargetClass, JumpPadBindings::TargetPointNames))
    {
        OutReason = NSLOCTEXT(
            "GameplayValidator",
            "JumpPadClassContractMismatch",
            "JumpPad 슬롯에는 Target Point를 가진 JumpPad Blueprint를 등록해야 합니다.");
        return false;
    }

    OutReason = FText::GetEmpty();
    return true;
}

void FJumpPadValidationProvider::Validate(
    const FGameplayValidationContext& Context,
    TArray<FGameplayValidationIssue>& OutIssues)
{
    const TArray<TWeakObjectPtr<AActor>>& Actors = Context.GetActors(ProviderId, DefaultSlotId);

    for (const TWeakObjectPtr<AActor>& WeakActor : Actors)
    {
        AActor* JumpPad = WeakActor.Get();
        if (!IsValid(JumpPad))
        {
            continue;
        }

        AActor* TargetPoint = FGameplayValidationReflectionUtils::GetActorProperty(
            JumpPad,
            JumpPadBindings::TargetPointNames);

        // JUMPPAD_001: 기획자가 반드시 연결해야 하는 Target Point 누락
        if (!IsValid(TargetPoint))
        {
            FGameplayValidationUtils::AddIssue(
                OutIssues,
                TEXT("JUMPPAD_001"),
                ProviderId,
                DefaultSlotId,
                EGameplayValidationSeverity::Error,
                JumpPad,
                NSLOCTEXT("GameplayValidator", "JumpPadMissingTarget", "Target Point가 지정되지 않았습니다."),
                NSLOCTEXT("GameplayValidator", "JumpPadMissingTargetFix", "JumpPad가 이동시킬 Target Point Actor를 지정하세요."));
        }
        else
        {
            // JUMPPAD_002: 자기 자신을 Target으로 잘못 지정
            if (TargetPoint == JumpPad)
            {
                FGameplayValidationUtils::AddIssue(
                    OutIssues,
                    TEXT("JUMPPAD_002"),
                    ProviderId,
                    DefaultSlotId,
                    EGameplayValidationSeverity::Error,
                    JumpPad,
                    NSLOCTEXT("GameplayValidator", "JumpPadSelfTarget", "JumpPad 자기 자신이 Target Point로 지정되어 있습니다."),
                    NSLOCTEXT("GameplayValidator", "JumpPadSelfTargetFix", "별도의 Target Point Actor를 지정하세요."));
            }
            // JUMPPAD_003: Target은 있지만 이동 의미가 거의 없는 배치
            else if (JumpPad->GetActorLocation().Equals(TargetPoint->GetActorLocation(), 1.0f))
            {
                FGameplayValidationUtils::AddIssue(
                    OutIssues,
                    TEXT("JUMPPAD_003"),
                    ProviderId,
                    DefaultSlotId,
                    EGameplayValidationSeverity::Warning,
                    JumpPad,
                    NSLOCTEXT("GameplayValidator", "JumpPadSameLocation", "JumpPad와 Target Point가 사실상 같은 위치에 있습니다."),
                    NSLOCTEXT("GameplayValidator", "JumpPadSameLocationFix", "Target Point의 배치 위치가 의도한 위치인지 확인하세요."),
                    TargetPoint);
            }

        }

        // JUMPPAD_005: JumpPad 진입 위치 주변에 AI가 접근할 수 있는 NavMesh가 없는지 검사
        if (!JumpPadBindings::CanProjectToNavigation(Context.World, JumpPad->GetActorLocation()))
        {
            FGameplayValidationUtils::AddIssue(
                OutIssues,
                TEXT("JUMPPAD_005"),
                ProviderId,
                DefaultSlotId,
                EGameplayValidationSeverity::Warning,
                JumpPad,
                NSLOCTEXT("GameplayValidator", "JumpPadOffNav", "JumpPad 주변에서 유효한 NavMesh를 찾을 수 없습니다."),
                NSLOCTEXT("GameplayValidator", "JumpPadOffNavFix", "AI가 이 JumpPad에 접근해야 한다면 JumpPad를 NavMesh 주변으로 이동하세요."));
        }
    }
}
