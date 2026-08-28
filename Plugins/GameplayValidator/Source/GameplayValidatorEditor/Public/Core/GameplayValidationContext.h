#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationTypes.h"

class AActor;
class UWorld;

/** World를 한 번 스캔한 결과를 모든 Provider가 공유합니다. */
struct FGameplayValidationContext
{
    UWorld* World = nullptr;

    TMap<FGameplayValidationTargetKey, TArray<TWeakObjectPtr<AActor>>> ActorsByTarget;

    const TArray<TWeakObjectPtr<AActor>>& GetActors(const FName ProviderId, const FName SlotId) const
    {
        static const TArray<TWeakObjectPtr<AActor>> Empty;
        const FGameplayValidationTargetKey Key(ProviderId, SlotId);
        if (const TArray<TWeakObjectPtr<AActor>>* Found = ActorsByTarget.Find(Key))
        {
            return *Found;
        }
        return Empty;
    }

    int32 GetActorCount(const FName ProviderId, const FName SlotId) const
    {
        return GetActors(ProviderId, SlotId).Num();
    }

    int32 GetTotalActorCount() const
    {
        int32 Total = 0;
        for (const TPair<FGameplayValidationTargetKey, TArray<TWeakObjectPtr<AActor>>>& Pair : ActorsByTarget)
        {
            Total += Pair.Value.Num();
        }
        return Total;
    }
};
