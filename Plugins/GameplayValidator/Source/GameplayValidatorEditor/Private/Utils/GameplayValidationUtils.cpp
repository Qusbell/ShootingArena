#include "Utils/GameplayValidationUtils.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"

void FGameplayValidationUtils::AddIssue(
    TArray<FGameplayValidationIssue>& OutIssues,
    const FName RuleId,
    const FName ProviderId,
    const FName SlotId,
    const EGameplayValidationSeverity Severity,
    AActor* TargetActor,
    const FText& Message,
    const FText& Suggestion,
    AActor* RelatedActor)
{
    FGameplayValidationIssue& Issue = OutIssues.AddDefaulted_GetRef();
    Issue.RuleId = RuleId;
    Issue.ProviderId = ProviderId;
    Issue.SlotId = SlotId;
    Issue.Severity = Severity;
    Issue.TargetActor = TargetActor;
    Issue.RelatedActor = RelatedActor;
    Issue.Message = Message;
    Issue.Suggestion = Suggestion;
}

bool FGameplayValidationUtils::IsTriggerUsableForPawn(
    const UPrimitiveComponent* Trigger,
    FText& OutFailureReason)
{
    if (!IsValid(Trigger))
    {
        OutFailureReason = NSLOCTEXT("GameplayValidator", "TriggerMissing", "Trigger Component를 찾을 수 없습니다.");
        return false;
    }

    if (!Trigger->GetGenerateOverlapEvents())
    {
        OutFailureReason = NSLOCTEXT("GameplayValidator", "OverlapDisabled", "Generate Overlap Events가 꺼져 있습니다.");
        return false;
    }

    if (Trigger->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
    {
        OutFailureReason = NSLOCTEXT("GameplayValidator", "CollisionDisabled", "Trigger Collision이 NoCollision로 설정되어 있습니다.");
        return false;
    }

    if (Trigger->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Overlap)
    {
        OutFailureReason = NSLOCTEXT(
            "GameplayValidator",
            "PawnNotOverlap",
            "Trigger의 Pawn Collision Response가 Overlap이 아닙니다.");
        return false;
    }

    OutFailureReason = FText::GetEmpty();
    return true;
}
