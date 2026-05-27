#include "MyReconnectionGameMode.h"
#include "Kismet/GameplayStatics.h"

FString AMyReconnectionGameMode::InitNewPlayer(APlayerController* NewPlayerController,
    const FUniqueNetIdRepl& UniqueId,
    const FString& Options,
    const FString& Portal)
{
	FString ErrorMessage = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	if (ErrorMessage.IsEmpty() && NewPlayerController)
	{
		// 1. URL에서 토큰 파싱
		FString ExtractedToken = UGameplayStatics::ParseOption(Options, TEXT("Token"));

		// 2. [핵심] 플레이어 컨트롤러에게 인터페이스 메시지로 토큰을 툭 던집니다.
		// 만약 컨트롤러가 이 인터페이스를 구현 안 했다면, 그냥 아무 일도 일어나지 않고 안전하게 넘어갑니다 (던지기 장땡).
		if (NewPlayerController->GetClass()->ImplementsInterface(UReconnectionInterface::StaticClass()))
		{
			IReconnectionInterface::Execute_SetConnectionToken(NewPlayerController, ExtractedToken);
		}
	}

	return ErrorMessage;
}
