#include "GameplayValidatorSettings.h"

const FGameplayValidationTargetBinding* UGameplayValidatorSettings::FindBinding(
    const FName ProviderId,
    const FName SlotId) const
{
    return TargetBindings.FindByPredicate([ProviderId, SlotId](const FGameplayValidationTargetBinding& Binding)
    {
        return Binding.ProviderId == ProviderId && Binding.SlotId == SlotId;
    });
}

UClass* UGameplayValidatorSettings::LoadTargetClass(const FName ProviderId, const FName SlotId) const
{
    const FGameplayValidationTargetBinding* Binding = FindBinding(ProviderId, SlotId);
    return Binding ? Binding->TargetClass.LoadSynchronous() : nullptr;
}

void UGameplayValidatorSettings::SetTargetClass(
    const FName ProviderId,
    const FName SlotId,
    UClass* NewClass)
{
    FGameplayValidationTargetBinding* Binding = TargetBindings.FindByPredicate(
        [ProviderId, SlotId](const FGameplayValidationTargetBinding& Existing)
        {
            return Existing.ProviderId == ProviderId && Existing.SlotId == SlotId;
        });

    if (!Binding)
    {
        Binding = &TargetBindings.AddDefaulted_GetRef();
        Binding->ProviderId = ProviderId;
        Binding->SlotId = SlotId;
    }

    Binding->TargetClass = NewClass;
    SaveConfig();
}
