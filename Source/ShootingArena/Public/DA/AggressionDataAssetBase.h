#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include "AggressionDataAssetBase.generated.h"


UCLASS(BlueprintType, Blueprintable)
class SHOOTINGARENA_API UAggressionDataAssetBase : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:

    // 높이 차이를 점수화할 때 사용하는 기준값
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Aggression|Height",
        meta = (ClampMin = "0.0", UIMin = "0.0")
    )
    float HeightStandard = 200.0f;


    // 최종 공격성 점수에서 높이 점수가 차지하는 비중
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Aggression|Score Weight",
        meta = (ClampMin = "0.0", ClampMax = "1.0")
    )
    float HeightWeight = 0.15f;


    // 최종 공격성 점수에서 생존 점수가 차지하는 비중
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Aggression|Score Weight",
        meta = (ClampMin = "0.0", ClampMax = "1.0")
    )
    float SurvivalWeight = 0.45f;


    // 최종 공격성 점수에서 무기 점수가 차지하는 비중
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Aggression|Score Weight",
        meta = (ClampMin = "0.0", ClampMax = "1.0")
    )
    float WeaponWeight = 0.4f;


    // 생존 점수에서 체력이 차지하는 비중
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Aggression|Survival Weight",
        meta = (ClampMin = "0.0", ClampMax = "1.0")
    )
    float HealthWeight = 0.7f;


    // 생존 점수에서 방어구가 차지하는 비중
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Aggression|Survival Weight",
        meta = (ClampMin = "0.0", ClampMax = "1.0")
    )
    float ArmorWeight = 0.3f;


    // 무기 점수를 0~1 범위로 정규화할 때 사용하는 최대값
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Aggression|Weapon",
        meta = (ClampMin = "0.0", UIMin = "0.0")
    )
    float WeaponBaseMax = 1000.0f;


#if WITH_EDITOR

public:

    virtual void PostEditChangeProperty(
        FPropertyChangedEvent& PropertyChangedEvent
    ) override;


    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context
    ) const override;


private:

    void ShowValidationNotification(
        const FText& Message,
        TWeakPtr<SNotificationItem>& Notification
    );

    void DismissValidationNotification(
        TWeakPtr<SNotificationItem>& Notification
    );


    // 공격성 Weight용 경고
    TWeakPtr<SNotificationItem> AggressionWeightNotification;

    // 생존 Weight용 경고
    TWeakPtr<SNotificationItem> SurvivalWeightNotification;

#endif
};