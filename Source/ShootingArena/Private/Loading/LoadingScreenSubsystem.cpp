#include "Loading/LoadingScreenSubsystem.h"

#include "Loading/LoadingCoordinator.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "SlateBasics.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"

void ULoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (IsRunningDedicatedServer())
	{
		UE_LOG(LogTemp, Log, TEXT("[Loading] Screen subsystem skipped on dedicated server."));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[Loading] Screen subsystem initialized."));
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &ULoadingScreenSubsystem::HandlePreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ULoadingScreenSubsystem::HandlePostLoadMap);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ULoadingScreenSubsystem::Tick));
}

void ULoadingScreenSubsystem::Deinitialize()
{
	if (PreLoadMapHandle.IsValid()) FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	if (PostLoadMapHandle.IsValid()) FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	if (TickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	RemoveLoadingScreen();
	Super::Deinitialize();
}

void ULoadingScreenSubsystem::HandlePreLoadMap(const FString& MapName)
{
	if (IsRunningDedicatedServer()) return;
	UE_LOG(LogTemp, Log, TEXT("[Loading] PreLoadMap: %s"), *MapName);
	bTransitionActive = true;
	bSawCoordinator = false;
	SecondsSincePostLoad = 0.0f;
	ShowLoadingScreen(TEXT("맵을 불러오는 중..."));
	ApplyInputLock();
}

void ULoadingScreenSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!bTransitionActive || IsRunningDedicatedServer()) return;
	UE_LOG(LogTemp, Log, TEXT("[Loading] PostLoadMap: %s"), LoadedWorld ? *LoadedWorld->GetName() : TEXT("None"));
	SecondsSincePostLoad = 0.0f;
	ShowLoadingScreen(TEXT("게임 준비 중..."));
	ApplyInputLock();
}

void ULoadingScreenSubsystem::BeginExternalLoading(const FString& StatusText)
{
	if (IsRunningDedicatedServer()) return;
	UE_LOG(LogTemp, Log, TEXT("[Loading] External loading requested."));
	bTransitionActive = true;
	bExternalLoadingRequested = true;
	bSawCoordinator = false;
	SecondsSincePostLoad = 0.0f;
	ExternalLoadingStartWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	ShowLoadingScreen(StatusText);
	ApplyInputLock();
}

bool ULoadingScreenSubsystem::Tick(float DeltaSeconds)
{
	if (bFadingIn)
	{
		FadeElapsedSeconds += DeltaSeconds;
		const float Opacity = FMath::Clamp(FadeElapsedSeconds / 0.25f, 0.0f, 1.0f);
		if (LoadingOverlay.IsValid()) LoadingOverlay->SetRenderOpacity(Opacity);
		if (Opacity >= 1.0f) bFadingIn = false;
	}

	if (bFadingOut)
	{
		FadeElapsedSeconds += DeltaSeconds;
		const float Opacity = 1.0f - FMath::Clamp(FadeElapsedSeconds / 0.30f, 0.0f, 1.0f);
		if (LoadingOverlay.IsValid()) LoadingOverlay->SetRenderOpacity(Opacity);
		if (Opacity <= 0.0f)
		{
			RemoveLoadingScreen();
		}
		return true;
	}

	if (!bTransitionActive || IsRunningDedicatedServer()) return true;
	SecondsSincePostLoad += DeltaSeconds;
	ApplyInputLock();
	UpdateLoadingState();
	return true;
}

void ULoadingScreenSubsystem::UpdateLoadingState()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World) return;
	// 서버 생성 대기 중에는 아직 메뉴 월드에 남아 있습니다. 그 월드의 기존
	// Coordinator가 Ready여도 새 서버가 준비된 것은 아니므로 무시합니다.
	const bool bStillInExternalStartWorld = bExternalLoadingRequested && ExternalLoadingStartWorld.Get() == World;
	for (TActorIterator<ALoadingCoordinator> It(World); It; ++It)
	{
		if (bStillInExternalStartWorld)
		{
			break;
		}
		bSawCoordinator = true;
		switch (It->GetPhase())
		{
		case ELoadingCoordinatorPhase::Initializing:
			CurrentStatus = TEXT("게임 환경을 준비하는 중..."); return;
		case ELoadingCoordinatorPhase::WaitingForPlayers:
			CurrentStatus = FString::Printf(TEXT("참가자 준비 대기 중... (%d / %d)"), It->GetConnectedHumanPlayers(), It->GetExpectedHumanPlayers()); return;
		case ELoadingCoordinatorPhase::ReadyToPlay:
			HideLoadingScreen(); return;
		default:
			CurrentStatus = TEXT("전환을 완료하지 못했습니다."); return;
		}
	}

	// 로컬 메뉴 전환은 Coordinator가 없어도 곧바로 끝냅니다. 원격 클라이언트는
	// 복제 Actor가 도착할 시간을 충분히 준 뒤에만 안전장치로 해제합니다.
	if (bExternalLoadingRequested && !bSawCoordinator)
	{
		if (SecondsSincePostLoad < 45.0f)
		{
			// 호출자가 지정한 상태 문구(예: 메인 메뉴로 돌아가는 중)를 유지합니다.
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("[Loading] External loading timed out; releasing the local overlay."));
		HideLoadingScreen();
		return;
	}

	if (!bSawCoordinator && World->GetNetMode() != NM_Client && SecondsSincePostLoad >= 0.75f)
	{
		HideLoadingScreen();
	}
	else if (!bSawCoordinator && SecondsSincePostLoad >= 10.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Loading] Coordinator did not replicate within 10 seconds; releasing the local overlay."));
		HideLoadingScreen();
	}
}

void ULoadingScreenSubsystem::ShowLoadingScreen(const FString& StatusText)
{
	CurrentStatus = StatusText;
	if (!LoadingOverlay.IsValid())
	{
		LoadingOverlay = SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.01f, 0.01f, 0.015f, 1.0f))
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return GetStatusText(); })
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 28))
				.ColorAndOpacity(FLinearColor(0.92f, 0.72f, 0.25f, 1.0f))
			];
	}
	UGameViewportClient* ViewportClient = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	if (ViewportClient && !bOverlayAddedToViewport)
	{
		LoadingOverlay->SetRenderOpacity(0.0f);
		ViewportClient->AddViewportWidgetContent(LoadingOverlay.ToSharedRef(), 10000);
		bOverlayAddedToViewport = true;
		bFadingIn = true;
		bFadingOut = false;
		FadeElapsedSeconds = 0.0f;
		UE_LOG(LogTemp, Log, TEXT("[Loading] Overlay added to the local game viewport."));
	}
}

void ULoadingScreenSubsystem::HideLoadingScreen()
{
	bTransitionActive = false;
	bExternalLoadingRequested = false;
	ExternalLoadingStartWorld.Reset();
	if (LoadingOverlay.IsValid() && bOverlayAddedToViewport)
	{
		bFadingIn = false;
		bFadingOut = true;
		FadeElapsedSeconds = 0.0f;
		return;
	}
	RemoveLoadingScreen();
}

void ULoadingScreenSubsystem::RemoveLoadingScreen()
{
	bTransitionActive = false;
	bExternalLoadingRequested = false;
	ExternalLoadingStartWorld.Reset();
	bSawCoordinator = false;
	bFadingIn = false;
	bFadingOut = false;
	FadeElapsedSeconds = 0.0f;
	UGameViewportClient* ViewportClient = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	if (ViewportClient && LoadingOverlay.IsValid()) ViewportClient->RemoveViewportWidgetContent(LoadingOverlay.ToSharedRef());
	bOverlayAddedToViewport = false;
	ReleaseInputLock();
}

// SetIgnoreMoveInput/SetIgnoreLookInput 은 호출 횟수를 누적하는 카운터입니다.
// (true 를 N번 넣으면 false 도 N번 넣어야 풀림) ApplyInputLock 은 PreLoadMap /
// PostLoadMap / 매 Tick 에서 불리므로, 컨트롤러당 정확히 한 번만 잠그고
// ReleaseInputLock 에서 한 번만 풀어야 합니다. 그렇지 않으면 로딩 화면이
// 여러 프레임 유지될 때 카운터가 크게 쌓여서, 전환이 끝나도 이동·시점 입력이
// 영구히 무시되는 상태로 남습니다.
void ULoadingScreenSubsystem::ApplyInputLock()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!IsValid(PlayerController)) return;

	if (bInputLocked && InputLockedController.Get() == PlayerController)
	{
		return;
	}

	// 이전(다른) 컨트롤러를 잠갔던 상태면 먼저 그쪽을 정확히 한 번 풀어줍니다.
	if (bInputLocked && InputLockedController.IsValid())
	{
		InputLockedController->SetIgnoreMoveInput(false);
		InputLockedController->SetIgnoreLookInput(false);
	}

	PlayerController->SetIgnoreMoveInput(true);
	PlayerController->SetIgnoreLookInput(true);
	InputLockedController = PlayerController;
	bInputLocked = true;
}

void ULoadingScreenSubsystem::ReleaseInputLock()
{
	if (bInputLocked && InputLockedController.IsValid())
	{
		// 균형이 어긋난 경우(다른 시스템이 같은 카운터를 건드렸거나, 과거
		// 버전에서 누적된 값)에도 전환 직후 플레이어가 반드시 움직일 수 있도록
		// 카운터를 0으로 강제 초기화합니다. 이 서브시스템은 클라이언트 전용이고
		// 맵 전환 시점에만 실행되므로 다른 정상적인 입력 잠금과 충돌하지 않습니다.
		InputLockedController->ResetIgnoreMoveInput();
		InputLockedController->ResetIgnoreLookInput();
	}
	InputLockedController.Reset();
	bInputLocked = false;
}

FText ULoadingScreenSubsystem::GetStatusText() const
{
	return FText::FromString(CurrentStatus);
}
