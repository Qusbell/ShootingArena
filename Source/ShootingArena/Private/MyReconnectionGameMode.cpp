#include "MyReconnectionGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "ShootingArenaGameInstance.h"
#include "GameFramework/PlayerState.h"

FString AMyReconnectionGameMode::InitNewPlayer(APlayerController* NewPlayerController,
    const FUniqueNetIdRepl& UniqueId,
    const FString& Options,
    const FString& Portal)
{
	FString ErrorMessage = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	if (ErrorMessage.IsEmpty() && NewPlayerController)
	{
		// 1. URL���� ��ū �Ľ�
		FString ExtractedToken = UGameplayStatics::ParseOption(Options, TEXT("Token"));

		// 2. [�ٽ�] �÷��̾� ��Ʈ�ѷ����� �������̽� �޽����� ��ū�� �� �����ϴ�.
		// ���� ��Ʈ�ѷ��� �� �������̽��� ���� �� �ߴٸ�, �׳� �ƹ� �ϵ� �Ͼ�� �ʰ� �����ϰ� �Ѿ�ϴ� (������ �嶯).
		if (NewPlayerController->GetClass()->ImplementsInterface(UReconnectionInterface::StaticClass()))
		{
			IReconnectionInterface::Execute_SetConnectionToken(NewPlayerController, ExtractedToken);
		}

		// 이전 레벨(로비 등)에서 저장해둔 닉네임이 있으면 새 PlayerState에도 그대로 적용합니다.
		if (APlayerState* NewPlayerState = NewPlayerController->PlayerState)
		{
			if (UShootingArenaGameInstance* SAGameInstance = Cast<UShootingArenaGameInstance>(GetWorld() ? GetWorld()->GetGameInstance() : nullptr))
			{
				const FString SavedNickname = SAGameInstance->GetSavedNickname(NewPlayerState->SavedNetworkAddress);
				if (!SavedNickname.IsEmpty())
				{
					NewPlayerState->SetPlayerName(SavedNickname);
				}
			}
		}
	}

	return ErrorMessage;
}
