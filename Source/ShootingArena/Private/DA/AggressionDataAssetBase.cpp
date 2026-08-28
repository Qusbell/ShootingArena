#include "DA/AggressionDataAssetBase.h"

#if WITH_EDITOR

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"


namespace AggressionDataAssetValidation
{
    /**
     * Weight처럼 지속적으로 추적할 필요가 없는 값에 사용하는
     * 단순 일회성 Notification입니다.
     */
    void ShowTransientNotification(const FText& Message)
    {
        FNotificationInfo NotificationInfo(Message);

        NotificationInfo.bFireAndForget = true;
        NotificationInfo.ExpireDuration = 4.0f;
        NotificationInfo.FadeOutDuration = 0.25f;

        const TSharedPtr<SNotificationItem> Notification =
            FSlateNotificationManager::Get().AddNotification(
                NotificationInfo
            );

        if (Notification.IsValid())
        {
            Notification->SetCompletionState(
                SNotificationItem::CS_Fail
            );
        }
    }
}


void UAggressionDataAssetBase::PostEditChangeProperty(
    FPropertyChangedEvent& PropertyChangedEvent
)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);


    // ------------------------------------------------------------
    // 슬라이더를 드래그하는 동안에는 검증하지 않습니다.
    //
    // Interactive 변경까지 매번 처리하면
    // Notification이 지나치게 자주 갱신될 수 있습니다.
    // ------------------------------------------------------------

    if (PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive)
    {
        return;
    }


    const FName PropertyName =
        PropertyChangedEvent.GetPropertyName();


    // ============================================================
    // Aggression Score Weight 검증
    //
    // HeightWeight
    // + SurvivalWeight
    // + WeaponWeight
    // == 1.0
    // ============================================================

    const bool bAggressionWeightChanged =
        PropertyName == GET_MEMBER_NAME_CHECKED(
            UAggressionDataAssetBase,
            HeightWeight
        )
        ||
        PropertyName == GET_MEMBER_NAME_CHECKED(
            UAggressionDataAssetBase,
            SurvivalWeight
        )
        ||
        PropertyName == GET_MEMBER_NAME_CHECKED(
            UAggressionDataAssetBase,
            WeaponWeight
        );


    if (bAggressionWeightChanged)
    {
        const float AggressionWeightSum =
            HeightWeight +
            SurvivalWeight +
            WeaponWeight;


        if (!FMath::IsNearlyEqual(
            AggressionWeightSum,
            1.0f,
            KINDA_SMALL_NUMBER
        ))
        {
            // 합계가 잘못된 경우 경고를 표시합니다.
            //
            // 이미 경고가 떠 있다면
            // ShowValidationNotification() 내부에서
            // 기존 경고를 지우고 새 내용으로 갱신합니다.
            ShowValidationNotification(
                FText::Format(
                    NSLOCTEXT(
                        "AggressionDataAsset",
                        "AggressionWeightImmediateWarning",
                        "Height / Survival / Weapon Weight의 합은 "
                        "1.0이어야 합니다. 현재 합: {0}"
                    ),
                    FText::AsNumber(AggressionWeightSum)
                ),
                AggressionWeightNotification
            );
        }
        else
        {
            // 합계가 다시 1.0이 된 순간
            // 기존 경고를 즉시 제거합니다.
            DismissValidationNotification(
                AggressionWeightNotification
            );
        }
    }


    // ============================================================
    // Survival Score Weight 검증
    //
    // HealthWeight
    // + ArmorWeight
    // == 1.0
    // ============================================================

    const bool bSurvivalWeightChanged =
        PropertyName == GET_MEMBER_NAME_CHECKED(
            UAggressionDataAssetBase,
            HealthWeight
        )
        ||
        PropertyName == GET_MEMBER_NAME_CHECKED(
            UAggressionDataAssetBase,
            ArmorWeight
        );


    if (bSurvivalWeightChanged)
    {
        const float SurvivalWeightSum =
            HealthWeight +
            ArmorWeight;


        if (!FMath::IsNearlyEqual(
            SurvivalWeightSum,
            1.0f,
            KINDA_SMALL_NUMBER
        ))
        {
            ShowValidationNotification(
                FText::Format(
                    NSLOCTEXT(
                        "AggressionDataAsset",
                        "SurvivalWeightImmediateWarning",
                        "Health / Armor Weight의 합은 "
                        "1.0이어야 합니다. 현재 합: {0}"
                    ),
                    FText::AsNumber(SurvivalWeightSum)
                ),
                SurvivalWeightNotification
            );
        }
        else
        {
            // 합계가 다시 1.0이 된 순간
            // 기존 경고를 즉시 제거합니다.
            DismissValidationNotification(
                SurvivalWeightNotification
            );
        }
    }


    // ============================================================
    // Height Standard 즉시 검증
    // ============================================================

    if (
        PropertyName == GET_MEMBER_NAME_CHECKED(
            UAggressionDataAssetBase,
            HeightStandard
        )
        &&
        HeightStandard <= 0.0f
        )
    {
        AggressionDataAssetValidation::ShowTransientNotification(
            NSLOCTEXT(
                "AggressionDataAsset",
                "HeightStandardImmediateWarning",
                "Height Standard는 0보다 커야 합니다."
            )
        );
    }


    // ============================================================
    // Weapon Base Max 즉시 검증
    // ============================================================

    if (
        PropertyName == GET_MEMBER_NAME_CHECKED(
            UAggressionDataAssetBase,
            WeaponBaseMax
        )
        &&
        WeaponBaseMax <= 0.0f
        )
    {
        AggressionDataAssetValidation::ShowTransientNotification(
            NSLOCTEXT(
                "AggressionDataAsset",
                "WeaponBaseMaxImmediateWarning",
                "Weapon Base Max는 0보다 커야 합니다."
            )
        );
    }
}


void UAggressionDataAssetBase::ShowValidationNotification(
    const FText& Message,
    TWeakPtr<SNotificationItem>& Notification
)
{
    // ------------------------------------------------------------
    // 이미 같은 종류의 Notification이 떠 있다면 제거합니다.
    //
    // 예:
    // 현재 합 1.1 → 현재 합 1.2
    //
    // 기존 1.1 경고를 그대로 남겨놓지 않고
    // 1.2라는 최신 상태를 보여주기 위함입니다.
    // ------------------------------------------------------------

    if (const TSharedPtr<SNotificationItem> ExistingNotification =
        Notification.Pin())
    {
        ExistingNotification->ExpireAndFadeout();
    }


    FNotificationInfo NotificationInfo(Message);


    // ------------------------------------------------------------
    // 자동으로 사라지지 않게 합니다.
    //
    // 합계가 정상 상태(1.0)로 돌아왔을 때
    // DismissValidationNotification()에서 직접 제거합니다.
    // ------------------------------------------------------------

    NotificationInfo.bFireAndForget = false;
    NotificationInfo.FadeOutDuration = 0.25f;


    const TSharedPtr<SNotificationItem> NewNotification =
        FSlateNotificationManager::Get().AddNotification(
            NotificationInfo
        );


    if (NewNotification.IsValid())
    {
        NewNotification->SetCompletionState(
            SNotificationItem::CS_Fail
        );

        Notification = NewNotification;
    }
}


void UAggressionDataAssetBase::DismissValidationNotification(
    TWeakPtr<SNotificationItem>& Notification
)
{
    // 현재 떠 있는 Notification이 존재하면 제거합니다.
    if (const TSharedPtr<SNotificationItem> ExistingNotification =
        Notification.Pin())
    {
        ExistingNotification->ExpireAndFadeout();
    }


    // 더 이상 유효한 경고를 가리키지 않도록 참조도 초기화합니다.
    Notification.Reset();
}


EDataValidationResult UAggressionDataAssetBase::IsDataValid(
    FDataValidationContext& Context
) const
{
    const EDataValidationResult SuperResult =
        Super::IsDataValid(Context);


    bool bIsValid =
        SuperResult != EDataValidationResult::Invalid;


    // ============================================================
    // Aggression Score Weight 합계 검증
    //
    // HeightWeight
    // + SurvivalWeight
    // + WeaponWeight
    // == 1.0
    // ============================================================

    const float AggressionWeightSum =
        HeightWeight +
        SurvivalWeight +
        WeaponWeight;


    if (!FMath::IsNearlyEqual(
        AggressionWeightSum,
        1.0f,
        KINDA_SMALL_NUMBER
    ))
    {
        Context.AddError(
            FText::Format(
                NSLOCTEXT(
                    "AggressionDataAsset",
                    "InvalidAggressionWeightSum",
                    "Height / Survival / Weapon Weight의 합은 "
                    "1.0이어야 합니다. 현재 합: {0}"
                ),
                FText::AsNumber(AggressionWeightSum)
            )
        );

        bIsValid = false;
    }


    // ============================================================
    // Survival Score Weight 합계 검증
    //
    // HealthWeight
    // + ArmorWeight
    // == 1.0
    // ============================================================

    const float SurvivalWeightSum =
        HealthWeight +
        ArmorWeight;


    if (!FMath::IsNearlyEqual(
        SurvivalWeightSum,
        1.0f,
        KINDA_SMALL_NUMBER
    ))
    {
        Context.AddError(
            FText::Format(
                NSLOCTEXT(
                    "AggressionDataAsset",
                    "InvalidSurvivalWeightSum",
                    "Health / Armor Weight의 합은 "
                    "1.0이어야 합니다. 현재 합: {0}"
                ),
                FText::AsNumber(SurvivalWeightSum)
            )
        );

        bIsValid = false;
    }


    // ============================================================
    // Height Standard 검증
    // ============================================================

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


    // ============================================================
    // Weapon Base Max 검증
    // ============================================================

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


    // ============================================================
    // 최종 결과
    // ============================================================

    return bIsValid
        ? EDataValidationResult::Valid
        : EDataValidationResult::Invalid;
}

#endif