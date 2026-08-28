#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationContext.h"
#include "Core/GameplayValidationProviderRegistry.h"

class UWorld;

struct FGameplayValidationScanMessage
{
    FGameplayValidationTargetKey Key;
    FText Message;
};

/** 현재 Editor World를 정확히 한 번 순회하여 등록된 BP 인스턴스만 분류합니다. */
class FGameplayValidationWorldScanner
{
public:
    static bool BuildContext(
        UWorld* World,
        const FGameplayValidationProviderRegistry& Registry,
        FGameplayValidationContext& OutContext,
        TArray<FGameplayValidationScanMessage>& OutConfigurationMessages);
};
