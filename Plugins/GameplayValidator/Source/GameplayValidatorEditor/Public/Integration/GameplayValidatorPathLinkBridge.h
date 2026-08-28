#pragma once

#include "CoreMinimal.h"

class AActor;
class UClass;

/**
 * GameplayValidator의 Editor 모듈이 프로젝트 Runtime 모듈을 직접 의존하지 않도록 하는 Native Bridge입니다.
 *
 * ShootingArena의 Editor 빌드가 APathLink를 직접 사용해 Callback을 등록하고,
 * PathLink Provider는 이 Bridge를 통해 실제 APathLink API를 호출합니다.
 *
 * Bridge가 등록되지 않은 경우 Provider는 PathLink 자체 Validation Error를 만들지 않습니다.
 * 따라서 연결 실패가 Gameplay Error로 오인되는 false positive를 만들지 않습니다.
 */
class GAMEPLAYVALIDATOREDITOR_API FGameplayValidatorPathLinkBridge
{
public:
    struct FCallbacks
    {
        /** 등록한 Class가 실제 프로젝트 PathLink Class인지 확인합니다. */
        TFunction<bool(UClass*)> CanHandleClass;

        /** 실제 APathLink::ValidateLink를 호출합니다. */
        TFunction<bool(AActor*, FText&)> ValidateLink;

        /** 실제 APathLink::GetExitActor를 호출합니다. */
        TFunction<AActor*(AActor*)> GetExitActor;

        /** 실제 APathLink::IsEnabled를 호출합니다. */
        TFunction<bool(AActor*)> IsEnabled;
    };

    static void Register(FCallbacks&& InCallbacks);
    static void Unregister();

    static bool IsRegistered();
    static bool CanHandleClass(UClass* Class);

    /** Callback 호출 자체에 성공하면 true. OutValid는 PathLink Validation 결과입니다. */
    static bool TryValidateLink(AActor* Actor, bool& OutValid, FText& OutFailureReason);

    /** Callback 호출 자체에 성공하면 true. Marker이면 OutExitActor가 nullptr일 수 있습니다. */
    static bool TryGetExitActor(AActor* Actor, AActor*& OutExitActor);

    /** Callback 호출 자체에 성공하면 true. */
    static bool TryIsEnabled(AActor* Actor, bool& OutEnabled);
};
