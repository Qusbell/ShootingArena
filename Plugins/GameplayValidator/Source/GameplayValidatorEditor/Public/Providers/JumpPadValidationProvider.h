#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationProvider.h"

class FJumpPadValidationProvider final : public IGameplayValidationProvider
{
public:
    static const FName ProviderId;
    static const FName DefaultSlotId;

    virtual FName GetProviderId() const override { return ProviderId; }
    virtual FText GetDisplayName() const override;
    virtual void GetTargetSlots(TArray<FGameplayValidationTargetSlot>& OutSlots) const override;
    virtual bool ValidateTargetClass(FName SlotId, UClass* TargetClass, FText& OutReason) const override;
    virtual void Validate(const FGameplayValidationContext& Context, TArray<FGameplayValidationIssue>& OutIssues) override;
};
