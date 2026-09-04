#include "Loading/LoadingBlueprintLibrary.h"

#include "Loading/LoadingCoordinator.h"
#include "Loading/LoadingScreenSubsystem.h"
#include "LocalDedicatedServerLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

void ULoadingBlueprintLibrary::BeginLoadingScreen(const UObject* WorldContextObject, const FString& StatusText)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World) return;
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		GameInstance->GetSubsystem<ULoadingScreenSubsystem>()->BeginExternalLoading(StatusText);
	}
}

void ULoadingBlueprintLibrary::BeginLoadingAndOpenLevel(const UObject* WorldContextObject, const FString& LevelName, const FString& StatusText)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || LevelName.IsEmpty()) return;

	BeginLoadingScreen(WorldContextObject, StatusText);
	TWeakObjectPtr<UGameInstance> WeakGameInstance = World->GetGameInstance();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakGameInstance, LevelName](float)
	{
		UGameInstance* GameInstance = WeakGameInstance.Get();
		UWorld* CurrentWorld = GameInstance ? GameInstance->GetWorld() : nullptr;
		if (CurrentWorld)
		{
			UGameplayStatics::SetGamePaused(CurrentWorld, false);
			UGameplayStatics::OpenLevel(CurrentWorld, FName(*LevelName));
		}
		return false;
	}), 0.05f);
}

void ULoadingBlueprintLibrary::BeginLoadingAndReturnToMainMenu(const UObject* WorldContextObject, const FString& StatusText)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World) return;

	// 원격 서버(통일 매치 서버 등)에 붙어 있는 경우, disconnect 를 지연 없이 즉시 실행합니다.
	// 안 그러면 다른 플레이어가 결과창의 "로비로"(서버 전체 ServerTravel)를 누를 때
	// 이 클라이언트가 disconnect 되기 전에 함께 로비로 끌려갑니다.
	if (World->GetNetMode() == NM_Client)
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			UGameplayStatics::SetGamePaused(World, false);
			PlayerController->ConsoleCommand(TEXT("disconnect"));
		}
	}

	BeginLoadingScreen(WorldContextObject, StatusText);
	TWeakObjectPtr<UGameInstance> WeakGameInstance = World->GetGameInstance();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakGameInstance](float)
	{
		// StopLocalDedicatedServer는 프로세스 종료를 기다릴 수 있습니다. 화면이 먼저
		// 그려진 다음 호출해야 기존 게임 화면이 멈춘 채 노출되지 않습니다.
		ULocalDedicatedServerLibrary::StopLocalDedicatedServer();
		UGameInstance* GameInstance = WeakGameInstance.Get();
		UWorld* CurrentWorld = GameInstance ? GameInstance->GetWorld() : nullptr;
		if (CurrentWorld)
		{
			UGameplayStatics::SetGamePaused(CurrentWorld, false);
			UGameplayStatics::OpenLevel(CurrentWorld, FName(TEXT("MainMenu_Level")));
		}
		return false;
	}), 0.40f);
}

void ULoadingBlueprintLibrary::BeginLoadingAndLeaveMatchToMainMenu(const UObject* WorldContextObject, const FString& StatusText)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World) return;

	// disconnect 를 지연 없이 "즉시" 실행합니다. 통일 서버 모델에서는 다른 플레이어가 결과창의
	// "로비로"(서버 전체 ServerTravel)를 누르면, 이 클라이언트가 disconnect 되기 전에 함께
	// 로비로 끌려가버립니다. 먼저 연결을 끊어야 "메인메뉴로"가 개별 동작으로 확정됩니다.
	if (APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		UGameplayStatics::SetGamePaused(World, false);
		PlayerController->ConsoleCommand(TEXT("disconnect"));
	}

	BeginLoadingScreen(WorldContextObject, StatusText);
	TWeakObjectPtr<UGameInstance> WeakGameInstance = World->GetGameInstance();
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakGameInstance](float)
	{
		// StopMatchServer는 프로세스 종료를 기다릴 수 있으므로, 오버레이가 렌더링된 뒤 실행합니다.
		ULocalDedicatedServerLibrary::StopMatchServer();
		UGameInstance* GameInstance = WeakGameInstance.Get();
		UWorld* CurrentWorld = GameInstance ? GameInstance->GetWorld() : nullptr;
		if (CurrentWorld)
		{
			UGameplayStatics::SetGamePaused(CurrentWorld, false);
			UGameplayStatics::OpenLevel(CurrentWorld, FName(TEXT("MainMenu_Level")));
		}
		return false;
	}), 0.40f);
}

void ULoadingBlueprintLibrary::MarkServerWorldReady(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client) return;
	for (TActorIterator<ALoadingCoordinator> It(World); It; ++It)
	{
		It->MarkServerWorldReady();
		return;
	}
}

int32 ULoadingBlueprintLibrary::GetConnectedHumanPlayerCount(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState) return 0;
	int32 Count = 0;
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (IsValid(PlayerState) && IsValid(PlayerState->GetPlayerController())) ++Count;
	}
	return Count;
}
