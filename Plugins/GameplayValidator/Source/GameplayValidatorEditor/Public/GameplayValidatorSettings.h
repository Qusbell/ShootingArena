#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayValidatorSettings.generated.h"

USTRUCT()
struct FGameplayValidationTargetBinding
{
    GENERATED_BODY()

    UPROPERTY(Config)
    FName ProviderId = NAME_None;

    UPROPERTY(Config)
    FName SlotId = NAME_None;

    /** Content Browser의 Blueprint Generated Class를 저장합니다. */
    UPROPERTY(Config)
    TSoftClassPtr<AActor> TargetClass;
};

/**
 * 툴 창에서 등록한 Blueprint Class를 저장합니다.
 * 검사 규칙 수치가 아니라 "무엇을 검사할지"만 보관합니다.
 */
UCLASS(Config=EditorPerProjectUserSettings)
class GAMEPLAYVALIDATOREDITOR_API UGameplayValidatorSettings : public UObject
{
    GENERATED_BODY()

public:
    const FGameplayValidationTargetBinding* FindBinding(FName ProviderId, FName SlotId) const;
    UClass* LoadTargetClass(FName ProviderId, FName SlotId) const;
    void SetTargetClass(FName ProviderId, FName SlotId, UClass* NewClass);

    UPROPERTY(Config)
    TArray<FGameplayValidationTargetBinding> TargetBindings;
};
