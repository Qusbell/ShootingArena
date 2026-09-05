#include "MyReconnectionGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "ShootingArenaGameInstance.h"
#include "MyPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "TimerManager.h"

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

	// 새 플레이어가 들어오면(재접속 포함) "로비로" 투표 캐시(Eligible 수 등)를 갱신해서
	// 결과창이 뜨기 전부터 이미 정확한 값(예: "0/2")이 준비되어 있게 합니다.
	// 이 시점엔 아직 GameState->PlayerArray에 이 플레이어(또는 동시에 들어오는 다른 플레이어)가
	// 완전히 반영되지 않았을 수 있어서, 다음 틱으로 미뤄 확실히 정착된 뒤에 계산합니다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &AMyReconnectionGameMode::CheckReturnToLobbyVotes));
	}
}

void AMyReconnectionGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &AMyReconnectionGameMode::CheckReturnToLobbyVotes));
	}
}

void AMyReconnectionGameMode::CheckReturnToLobbyVotes()
{
	int32 Voted = 0;
	int32 Eligible = 0;
	AMyPlayerState::ComputeReturnToLobbyVotes(GetWorld(), Voted, Eligible);

	// 일반 프로퍼티 리플리케이션(bWantsReturnToLobby)이 클라이언트에 지연/누락되는 문제가 있어,
	// 여기서 계산한 최신 값을 모든 PlayerState에 Multicast로 직접 방송해 UI가 즉시 갱신되게 합니다.
	if (const AGameStateBase* MyGameState = GetGameState<AGameStateBase>())
	{
		for (const APlayerState* PS : MyGameState->PlayerArray)
		{
			if (AMyPlayerState* MyPS = const_cast<AMyPlayerState*>(Cast<AMyPlayerState>(PS)))
			{
				MyPS->Multicast_UpdateReturnToLobbyVoteCounts(Voted, Eligible);
			}
		}
	}

	// 접속한 인간 플레이어가 없거나, 전원이 투표하지 않았다면 아무것도 하지 않습니다.
	if (Eligible <= 0 || Voted < Eligible)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!World->NextURL.IsEmpty() || World->IsInSeamlessTravel())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Match] 전원(%d/%d) 로비로 투표 완료 -> ServerTravel"), Voted, Eligible);
	World->ServerTravel(TEXT("/Game/QuakeLike_1_0/GameFlow/Lobby/Lobby_Level"), true);
}
