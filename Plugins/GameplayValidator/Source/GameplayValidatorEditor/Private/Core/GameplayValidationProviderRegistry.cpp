#include "Core/GameplayValidationProviderRegistry.h"

#include "Providers/JumpPadValidationProvider.h"
#include "Providers/PathLinkValidationProvider.h"
#include "Providers/PortalValidationProvider.h"

FGameplayValidationProviderRegistry::FGameplayValidationProviderRegistry()
{
    RegisterProvider(MakeShared<FPortalValidationProvider>());
    RegisterProvider(MakeShared<FJumpPadValidationProvider>());
    RegisterProvider(MakeShared<FPathLinkValidationProvider>());
}

TSharedPtr<IGameplayValidationProvider> FGameplayValidationProviderRegistry::FindProvider(const FName ProviderId) const
{
    for (const TSharedRef<IGameplayValidationProvider>& Provider : Providers)
    {
        if (Provider->GetProviderId() == ProviderId)
        {
            return Provider;
        }
    }
    return nullptr;
}


bool FGameplayValidationProviderRegistry::RegisterProvider(TSharedRef<IGameplayValidationProvider> Provider)
{
    const FName ProviderId = Provider->GetProviderId();
    if (ProviderId.IsNone() || FindProvider(ProviderId).IsValid())
    {
        return false;
    }

    Providers.Add(MoveTemp(Provider));
    return true;
}

bool FGameplayValidationProviderRegistry::UnregisterProvider(const FName ProviderId)
{
    const int32 Removed = Providers.RemoveAll([ProviderId](const TSharedRef<IGameplayValidationProvider>& Provider)
    {
        return Provider->GetProviderId() == ProviderId;
    });
    return Removed > 0;
}
