
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MyPlayerController.generated.h"

/**
 * 
 */
UCLASS()
class SHOOTINGARENA_API AMyPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	virtual void PawnLeavingGame() override;
	

	// 게임을 멈추거나 재개해달라고 서버에 요청하는 함수.
    // 클라이언트가 이 함수를 호출하면, 실제 실행은 서버에서 대신 해줌.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Pause")
	void ServerSetGamePaused(bool bShouldPause);
};
