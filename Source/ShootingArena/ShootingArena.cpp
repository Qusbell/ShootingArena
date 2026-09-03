// Fill out your copyright notice in the Description page of Project Settings.

#include "ShootingArena.h"
#include "Loading/LoadingScreenSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
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

        // GameInstanceSubsystem은 원칙적으로 자동 생성되지만, Standalone/PIE의
        // 게임 인스턴스 생성 순서에 영향을 받지 않도록 게임 월드가 준비되는 즉시
        // 명시적으로 확보합니다. 이 호출은 기존 BP나 게임플레이 로직을 바꾸지 않습니다.
        PostWorldInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda(
            [](UWorld* World, const UWorld::InitializationValues)
            {
                if (!IsValid(World) || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
                {
                    return;
                }

                if (UGameInstance* GameInstance = World->GetGameInstance())
                {
                    GameInstance->GetSubsystem<ULoadingScreenSubsystem>();
                }
            });

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
        if (PostWorldInitializationHandle.IsValid())
        {
            FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitializationHandle);
            PostWorldInitializationHandle.Reset();
        }

#if WITH_EDITOR
        FGameplayValidatorPathLinkBridge::Unregister();
#endif

        FDefaultGameModuleImpl::ShutdownModule();
    }

private:
    FDelegateHandle PostWorldInitializationHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FShootingArenaGameModule, ShootingArena, "ShootingArena");
