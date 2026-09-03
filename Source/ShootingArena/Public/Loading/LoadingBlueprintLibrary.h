#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LoadingBlueprintLibrary.generated.h"

/** 기존 Blueprint에 최소한의 노드만 추가하기 위한 로딩 시스템 진입점입니다. */
UCLASS()
class SHOOTINGARENA_API ULoadingBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Loading", meta = (WorldContext = "WorldContextObject"))
	static void BeginLoadingScreen(const UObject* WorldContextObject, const FString& StatusText);

	/** 로딩 화면을 먼저 한 프레임 이상 표시한 뒤 로컬 맵을 엽니다. Pause 중에도 안전합니다. */
	UFUNCTION(BlueprintCallable, Category = "Loading", meta = (WorldContext = "WorldContextObject"))
	static void BeginLoadingAndOpenLevel(const UObject* WorldContextObject, const FString& LevelName, const FString& StatusText);

	/** 캠페인 종료용: 로딩 화면을 먼저 표시한 뒤 로컬 전용 서버를 종료하고 메인 메뉴를 엽니다. */
	UFUNCTION(BlueprintCallable, Category = "Loading", meta = (WorldContext = "WorldContextObject"))
	static void BeginLoadingAndReturnToMainMenu(const UObject* WorldContextObject, const FString& StatusText);

	/** 멀티플레이 로비 종료용: 화면을 그린 뒤 매치 서버를 종료하고 연결을 끊은 다음 메인 메뉴를 엽니다. */
	UFUNCTION(BlueprintCallable, Category = "Loading", meta = (WorldContext = "WorldContextObject"))
	static void BeginLoadingAndLeaveMatchToMainMenu(const UObject* WorldContextObject, const FString& StatusText);

	UFUNCTION(BlueprintCallable, Category = "Loading", meta = (WorldContext = "WorldContextObject"))
	static void MarkServerWorldReady(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Loading", meta = (WorldContext = "WorldContextObject"))
	static int32 GetConnectedHumanPlayerCount(const UObject* WorldContextObject);
};
