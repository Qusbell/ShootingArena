#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationTypes.h"

class AActor;

/** Error / Warning 한 건을 UI와 Log에서 공통으로 사용합니다. */
struct FGameplayValidationIssue
{
    FName RuleId = NAME_None;
    FName ProviderId = NAME_None;
    FName SlotId = NAME_None;
    EGameplayValidationSeverity Severity = EGameplayValidationSeverity::Warning;

    // Validator가 Actor 수명을 소유하지 않도록 Weak Pointer를 사용합니다.
    TWeakObjectPtr<AActor> TargetActor;
    TWeakObjectPtr<AActor> RelatedActor;

    FText Message;
    FText Suggestion;
};
