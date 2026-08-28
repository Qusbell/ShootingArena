#include "DA/AggressionDataAssetBase.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif


#if WITH_EDITOR

EDataValidationResult UAggressionDataAssetBase::IsDataValid(
    FDataValidationContext& Context
) const
{
    const EDataValidationResult SuperResult =
        Super::IsDataValid(Context);

    bool bIsValid =
        SuperResult != EDataValidationResult::Invalid;


    // ------------------------------------------------------------
    // 공격성 점수 Weight 합계 검증
    //
    // HeightWeight
    // + SurvivalWeight
    // + WeaponWeight
    // == 1.0
    // ------------------------------------------------------------

    constexpr float ExpectedAggressionWeightSum = 1.0f;

    const float AggressionWeightSum =
        HeightWeight +
        SurvivalWeight +
        WeaponWeight;

    if (!FMath::IsNearlyEqual(
        AggressionWeightSum,
        ExpectedAggressionWeightSum,
        KINDA_SMALL_NUMBER))
    {
        Context.AddError(
            FText::Format(
                NSLOCTEXT(
                    "AggressionDataAsset",
                    "InvalidAggressionWeightSum",
                    "Height / Survival / Weapon Weight의 합은 1.0이어야 합니다. 현재 합: {0}"
                ),
                FText::AsNumber(AggressionWeightSum)
            )
        );

        bIsValid = false;
    }


    // ------------------------------------------------------------
    // 생존 점수 Weight 합계 검증
    //
    // HealthWeight
    // + ArmorWeight
    // == 1.0
    // ------------------------------------------------------------

    constexpr float ExpectedSurvivalWeightSum = 1.0f;

    const float SurvivalWeightSum =
        HealthWeight +
        ArmorWeight;

    if (!FMath::IsNearlyEqual(
        SurvivalWeightSum,
        ExpectedSurvivalWeightSum,
        KINDA_SMALL_NUMBER))
    {
        Context.AddError(
            FText::Format(
                NSLOCTEXT(
                    "AggressionDataAsset",
                    "InvalidSurvivalWeightSum",
                    "Health / Armor Weight의 합은 1.0이어야 합니다. 현재 합: {0}"
                ),
                FText::AsNumber(SurvivalWeightSum)
            )
        );

        bIsValid = false;
    }


    // ------------------------------------------------------------
    // Height Standard 검증
    // ------------------------------------------------------------

    if (HeightStandard <= 0.0f)
    {
        Context.AddError(
            NSLOCTEXT(
                "AggressionDataAsset",
                "InvalidHeightStandard",
                "Height Standard는 0보다 커야 합니다."
            )
        );

        bIsValid = false;
    }


    // ------------------------------------------------------------
    // Weapon Base Max 검증
    // ------------------------------------------------------------

    if (WeaponBaseMax <= 0.0f)
    {
        Context.AddError(
            NSLOCTEXT(
                "AggressionDataAsset",
                "InvalidWeaponBaseMax",
                "Weapon Base Max는 0보다 커야 합니다."
            )
        );

        bIsValid = false;
    }


    return bIsValid
        ? EDataValidationResult::Valid
        : EDataValidationResult::Invalid;
}

#endif