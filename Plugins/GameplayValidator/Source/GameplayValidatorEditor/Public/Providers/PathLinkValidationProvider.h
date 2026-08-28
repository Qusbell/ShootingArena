#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationProvider.h"

/**
 * PathLink 검사 Provider.
 *
 * 실제 PathLink Validation은 Reflection ProcessEvent를 사용하지 않습니다.
 * ShootingArena Editor 빌드에서 등록한 Native Bridge를 통해 APathLink의 공개 API를 직접 호출합니다.
 *
 * Bridge가 없는 경우에는 Route/Marker/Disabled 분류와 미사용 Marker Warning만 수행하고,
 * PathLink 자체 Validation Error는 만들지 않습니다. 연결 문제를 Gameplay Error로 오인하지 않기 위함입니다.
 */
class FPathLinkValidationProvider final : public IGameplayValidationProvider
{
public:
    static const FName ProviderId;
    static const FName DefaultSlotId;

    virtual FName GetProviderId() const override { return ProviderId; }
    virtual FText GetDisplayName() const override;
    virtual void GetTargetSlots(TArray<FGameplayValidationTargetSlot>& OutSlots) const override;
    virtual bool ValidateTargetClass(FName SlotId, UClass* TargetClass, FText& OutReason) const override;

    virtual void Prepare(const FGameplayValidationContext& Context) override;
    virtual void Validate(const FGameplayValidationContext& Context, TArray<FGameplayValidationIssue>& OutIssues) override;
    virtual void Finalize(const FGameplayValidationContext& Context, TArray<FGameplayValidationIssue>& OutIssues) override;
    virtual void GetSummaryMetrics(TArray<FGameplayValidationMetric>& OutMetrics) const override;

private:
    /** Native Bridge가 없을 때 Marker 분류만 안전하게 할 수 있도록 사용하는 읽기 전용 fallback입니다. */
    static AActor* ReadExitActorProperty(const AActor* Link);
    static bool ReadEnabledProperty(const AActor* Link, bool& OutEnabled);
    static bool HasFallbackPropertyContract(UClass* Class);

    TArray<TWeakObjectPtr<AActor>> Links;
    TArray<TWeakObjectPtr<AActor>> Markers;
    TSet<TWeakObjectPtr<AActor>> ReferencedExitActors;

    int32 RouteLinkCount = 0;
    int32 MarkerCount = 0;
    int32 DisabledCount = 0;
    int32 UnusedMarkerCount = 0;
};
