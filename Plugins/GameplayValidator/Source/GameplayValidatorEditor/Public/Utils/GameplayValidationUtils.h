#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationIssue.h"

class AActor;

class UPrimitiveComponent;

class FGameplayValidationUtils
{
public:
    static void AddIssue(
        TArray<FGameplayValidationIssue>& OutIssues,
        FName RuleId,
        FName ProviderId,
        FName SlotId,
        EGameplayValidationSeverity Severity,
        AActor* TargetActor,
        const FText& Message,
        const FText& Suggestion = FText::GetEmpty(),
        AActor* RelatedActor = nullptr);

    /** Overlap 이벤트 자체가 발생할 수 없는 명백한 Trigger 설정 오류만 검사합니다. */
    static bool IsTriggerUsableForPawn(const UPrimitiveComponent* Trigger, FText& OutFailureReason);
};
