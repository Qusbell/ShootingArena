#include "MyReconnectionGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "ShootingArenaGameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"

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
			if (!NicknameOption.IsEmpty())
			{
				NewPlayerState->SetPlayerName(NicknameOption);
			}
			// 2번(GameInstance 조회)은 PostLogin 에서 처리합니다 — 이 시점엔 SavedNetworkAddress 를 못 구합니다.
		}
	}

	return ErrorMessage;
}

void AMyReconnectionGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (NewPlayer && NewPlayer->PlayerState)
	{
		// 이 시점엔 SetPlayer() 로 NetConnection 이 PC 에 붙어 있어 주소를 구할 수 있습니다.
		// (":포트" 제거 — 재접속 시 클라이언트 포트가 바뀌므로 IP만 키로 씁니다.)
		FString NetworkAddress = NewPlayer->GetPlayerNetworkAddress();
		const int32 ColonPos = NetworkAddress.Find(TEXT(":"), ESearchCase::CaseSensitive);
		const FString StrippedAddress = (ColonPos > 0) ? NetworkAddress.Left(ColonPos) : NetworkAddress;
		NewPlayer->PlayerState->SavedNetworkAddress = StrippedAddress;

		// 이전 레벨(로비)에서 Server_SetPlayerName 으로 저장해둔 닉네임이 있으면 적용합니다.
		if (const UShootingArenaGameInstance* SAGameInstance = GetGameInstance<UShootingArenaGameInstance>())
		{
			const FString SavedNickname = SAGameInstance->GetSavedNickname(StrippedAddress);
			if (!SavedNickname.IsEmpty())
			{
				NewPlayer->PlayerState->SetPlayerName(SavedNickname);
			}
		}
	}

	Super::PostLogin(NewPlayer);
}
