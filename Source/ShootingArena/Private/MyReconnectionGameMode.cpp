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

		// 닉네임 적용. 우선순위:
		// 1. 접속 URL에 직접 실려온 "?Nickname=" 옵션 (로비 서버와 별도 프로세스인 매치 서버로
		//    접속할 때 이 방법을 씁니다 — 매치 서버는 로비와 GameInstance를 공유하지 않아서
		//    아래 2번 방법으로는 닉네임을 알 수 없기 때문입니다.)
		// 2. 이전 레벨(같은 프로세스 안에서의 로비 등)에서 저장해둔 닉네임 (GameInstance 조회)
		if (APlayerState* NewPlayerState = NewPlayerController->PlayerState)
		{
			const FString NicknameOption = UGameplayStatics::ParseOption(Options, TEXT("Nickname"));

			// [DEBUG] 임시 로그 - 문제 해결되면 제거 예정
			UE_LOG(LogTemp, Warning, TEXT("[NicknameDebug] Options=\"%s\" ParsedNickname=\"%s\""), *Options, *NicknameOption);

			if (!NicknameOption.IsEmpty())
			{
				NewPlayerState->SetPlayerName(NicknameOption);
			}
			else if (UShootingArenaGameInstance* SAGameInstance = Cast<UShootingArenaGameInstance>(GetWorld() ? GetWorld()->GetGameInstance() : nullptr))
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
