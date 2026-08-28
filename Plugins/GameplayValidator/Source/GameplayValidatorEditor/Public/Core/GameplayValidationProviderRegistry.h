#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationProvider.h"

/** Provider 추가/제거가 Core/UI 변경으로 번지지 않도록 등록 지점을 하나로 모읍니다. */
class FGameplayValidationProviderRegistry
{
public:
    FGameplayValidationProviderRegistry();

    const TArray<TSharedRef<IGameplayValidationProvider>>& GetProviders() const { return Providers; }
    TSharedPtr<IGameplayValidationProvider> FindProvider(FName ProviderId) const;

    /** 새 검사 시스템은 Provider 구현 후 이 Registry에 등록만 하면 Core/UI가 자동으로 따라갑니다. */
    bool RegisterProvider(TSharedRef<IGameplayValidationProvider> Provider);
    bool UnregisterProvider(FName ProviderId);

private:
    TArray<TSharedRef<IGameplayValidationProvider>> Providers;
};
