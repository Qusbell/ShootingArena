
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MyReconnectionGameMode.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UReconnectionInterface : public UInterface { GENERATED_BODY() };

class IReconnectionInterface
{
	GENERATED_BODY()

public:
	// 블루프린트에서 이 이벤트를 구현하여 토큰을 변수에 저장하도록 합니다.
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Reconnection")
	void SetConnectionToken(const FString& Token);

	// 블루프린트에서 저장된 토큰을 C++ 게임모드에게 다시 리턴해 주도록 합니다.
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Reconnection")
	FString GetConnectionToken() const;
};

/**
 * InitNewPlayer를 가로채서 접속과 함께 받은 Token을 저장하는 GameMode
 */
UCLASS()
class SHOOTINGARENA_API AMyReconnectionGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
protected:

	// 로그인 시점에 토큰을 파싱해서 컨트롤러에게 인터페이스로 던지기
	virtual FString InitNewPlayer(APlayerController* NewPlayerController,
		const FUniqueNetIdRepl& UniqueId,
		const FString& Options,
		const FString& Portal) override;
	
	
};
