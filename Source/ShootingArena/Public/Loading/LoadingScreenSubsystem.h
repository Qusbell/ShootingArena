#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LoadingScreenSubsystem.generated.h"

class SWidget;
class APlayerController;
class UWorld;

/** 모든 로컬 맵 전환을 감지해 Slate 로딩 화면을 표시하는 클라이언트 전용 서브시스템입니다. */
UCLASS()
class SHOOTINGARENA_API ULoadingScreenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void BeginExternalLoading(const FString& StatusText);

private:
	void HandlePreLoadMap(const FString& MapName);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	bool Tick(float DeltaSeconds);
	void ShowLoadingScreen(const FString& StatusText);
	void HideLoadingScreen();
	void RemoveLoadingScreen();
	void UpdateLoadingState();
	void ApplyInputLock();
	void ReleaseInputLock();
	FText GetStatusText() const;

	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	TSharedPtr<SWidget> LoadingOverlay;
	TWeakObjectPtr<APlayerController> InputLockedController;
	bool bInputLocked = false;
	FString CurrentStatus;
	float SecondsSincePostLoad = 0.0f;
	bool bTransitionActive = false;
	bool bExternalLoadingRequested = false;
	bool bSawCoordinator = false;
	TWeakObjectPtr<UWorld> ExternalLoadingStartWorld;
	bool bOverlayAddedToViewport = false;
	bool bFadingIn = false;
	bool bFadingOut = false;
	float FadeElapsedSeconds = 0.0f;
};
