#include "Integration/GameplayValidatorPathLinkBridge.h"

#include "GameFramework/Actor.h"

namespace
{
    TUniquePtr<FGameplayValidatorPathLinkBridge::FCallbacks> GPathLinkCallbacks;
}

void FGameplayValidatorPathLinkBridge::Register(FCallbacks&& InCallbacks)
{
    GPathLinkCallbacks = MakeUnique<FCallbacks>(MoveTemp(InCallbacks));
}

void FGameplayValidatorPathLinkBridge::Unregister()
{
    GPathLinkCallbacks.Reset();
}

bool FGameplayValidatorPathLinkBridge::IsRegistered()
{
    return GPathLinkCallbacks.IsValid()
        && static_cast<bool>(GPathLinkCallbacks->CanHandleClass)
        && static_cast<bool>(GPathLinkCallbacks->ValidateLink)
        && static_cast<bool>(GPathLinkCallbacks->GetExitActor)
        && static_cast<bool>(GPathLinkCallbacks->IsEnabled);
}

bool FGameplayValidatorPathLinkBridge::CanHandleClass(UClass* Class)
{
    return IsRegistered()
        && IsValid(Class)
        && GPathLinkCallbacks->CanHandleClass(Class);
}

bool FGameplayValidatorPathLinkBridge::TryValidateLink(
    AActor* Actor,
    bool& OutValid,
    FText& OutFailureReason)
{
    OutValid = true;
    OutFailureReason = FText::GetEmpty();

    if (!IsRegistered() || !IsValid(Actor) || !GPathLinkCallbacks->CanHandleClass(Actor->GetClass()))
    {
        return false;
    }

    OutValid = GPathLinkCallbacks->ValidateLink(Actor, OutFailureReason);
    return true;
}

bool FGameplayValidatorPathLinkBridge::TryGetExitActor(AActor* Actor, AActor*& OutExitActor)
{
    OutExitActor = nullptr;

    if (!IsRegistered() || !IsValid(Actor) || !GPathLinkCallbacks->CanHandleClass(Actor->GetClass()))
    {
        return false;
    }

    OutExitActor = GPathLinkCallbacks->GetExitActor(Actor);
    return true;
}

bool FGameplayValidatorPathLinkBridge::TryIsEnabled(AActor* Actor, bool& OutEnabled)
{
    OutEnabled = false;

    if (!IsRegistered() || !IsValid(Actor) || !GPathLinkCallbacks->CanHandleClass(Actor->GetClass()))
    {
        return false;
    }

    OutEnabled = GPathLinkCallbacks->IsEnabled(Actor);
    return true;
}
