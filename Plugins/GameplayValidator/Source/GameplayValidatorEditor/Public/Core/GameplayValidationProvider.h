#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationContext.h"
#include "Core/GameplayValidationIssue.h"
#include "Core/GameplayValidationTypes.h"
#include "GameFramework/Actor.h"

/**
 * 각 Gameplay 시스템의 지식은 Provider 내부에만 둡니다.
 * Core/UI는 Portal, JumpPad, PathLink를 직접 알지 않아도 됩니다.
 */
class IGameplayValidationProvider
{
public:
    virtual ~IGameplayValidationProvider() = default;

    virtual FName GetProviderId() const = 0;
    virtual FText GetDisplayName() const = 0;
    virtual void GetTargetSlots(TArray<FGameplayValidationTargetSlot>& OutSlots) const = 0;

    /** 등록된 Blueprint Class가 이 Provider가 기대하는 타입인지 검사합니다. */
    virtual bool ValidateTargetClass(FName SlotId, UClass* TargetClass, FText& OutReason) const
    {
        OutReason = FText::GetEmpty();
        return IsValid(TargetClass) && TargetClass->IsChildOf(AActor::StaticClass());
    }

    /** 전체 관계를 한 번만 계산해야 하는 Provider용 준비 단계입니다. */
    virtual void Prepare(const FGameplayValidationContext& Context) {}

    /** 개별 Actor/설정 검사를 수행합니다. */
    virtual void Validate(
        const FGameplayValidationContext& Context,
        TArray<FGameplayValidationIssue>& OutIssues) = 0;

    /** 미사용 Marker처럼 전체 Actor를 본 뒤 판단하는 검사를 수행합니다. */
    virtual void Finalize(
        const FGameplayValidationContext& Context,
        TArray<FGameplayValidationIssue>& OutIssues) {}

    /** UI Summary에 표시할 통계를 반환합니다. */
    virtual void GetSummaryMetrics(TArray<FGameplayValidationMetric>& OutMetrics) const {}
};
