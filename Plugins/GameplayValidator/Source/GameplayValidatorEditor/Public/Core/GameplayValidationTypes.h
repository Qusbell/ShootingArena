#pragma once

#include "CoreMinimal.h"

/** 기획자가 반드시 확인해야 하는 문제의 심각도입니다. */
enum class EGameplayValidationSeverity : uint8
{
    Error,
    Warning
};

/** Provider + Slot 조합으로 검사 대상을 식별합니다. 새 시스템이 추가되어도 enum 수정이 필요 없습니다. */
struct FGameplayValidationTargetKey
{
    FName ProviderId = NAME_None;
    FName SlotId = NAME_None;

    FGameplayValidationTargetKey() = default;

    FGameplayValidationTargetKey(const FName InProviderId, const FName InSlotId)
        : ProviderId(InProviderId)
        , SlotId(InSlotId)
    {
    }

    bool IsValid() const
    {
        return !ProviderId.IsNone() && !SlotId.IsNone();
    }

    bool operator==(const FGameplayValidationTargetKey& Other) const
    {
        return ProviderId == Other.ProviderId && SlotId == Other.SlotId;
    }
};

FORCEINLINE uint32 GetTypeHash(const FGameplayValidationTargetKey& Key)
{
    return HashCombine(GetTypeHash(Key.ProviderId), GetTypeHash(Key.SlotId));
}

/** Provider가 UI에 노출할 Blueprint 등록 칸 하나를 설명합니다. */
struct FGameplayValidationTargetSlot
{
    FName SlotId = NAME_None;
    FText DisplayName;
};

/** 검사 결과 상단에 표시할 가벼운 정수 통계입니다. */
struct FGameplayValidationMetric
{
    FName ProviderId = NAME_None;
    FName MetricId = NAME_None;
    FText DisplayName;
    int32 Value = 0;
    int32 SortOrder = 0;
};
