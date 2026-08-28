// Fill out your copyright notice in the Description page of Project Settings.

#include "ShootingArena.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "AI/PathLink/PathLink.h"
#include "Integration/GameplayValidatorPathLinkBridge.h"
#endif

/**
 * 기본 Game Module 동작은 그대로 유지하면서,
 * Editor 빌드에서만 Gameplay Validator와 PathLink Native Bridge를 연결합니다.
 * 게임플레이/패키징 로직에는 영향을 주지 않습니다.
 */
class FShootingArenaGameModule final : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();

#if WITH_EDITOR
        FGameplayValidatorPathLinkBridge::FCallbacks Callbacks;

        Callbacks.CanHandleClass = [](UClass* Class)
        {
            return IsValid(Class) && Class->IsChildOf(APathLink::StaticClass());
        };

        Callbacks.ValidateLink = [](AActor* Actor, FText& OutFailureReason)
        {
            const APathLink* PathLink = Cast<APathLink>(Actor);
            if (!IsValid(PathLink))
            {
                OutFailureReason = FText::GetEmpty();
                return true; // Bridge 오류를 Gameplay Error로 만들지 않습니다.
            }

            // Reflection이 아니라 실제 C++ APathLink API를 직접 호출합니다.
            return PathLink->ValidateLink(OutFailureReason);
        };

        Callbacks.GetExitActor = [](AActor* Actor) -> AActor*
        {
            const APathLink* PathLink = Cast<APathLink>(Actor);
            return IsValid(PathLink) ? PathLink->GetExitActor() : nullptr;
        };

        Callbacks.IsEnabled = [](AActor* Actor)
        {
            const APathLink* PathLink = Cast<APathLink>(Actor);
            return IsValid(PathLink) ? PathLink->IsEnabled() : false;
        };

        FGameplayValidatorPathLinkBridge::Register(MoveTemp(Callbacks));
#endif
    }

    virtual void ShutdownModule() override
    {
#if WITH_EDITOR
        FGameplayValidatorPathLinkBridge::Unregister();
#endif

        FDefaultGameModuleImpl::ShutdownModule();
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FShootingArenaGameModule, ShootingArena, "ShootingArena");
