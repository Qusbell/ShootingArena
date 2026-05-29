
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
	
};
