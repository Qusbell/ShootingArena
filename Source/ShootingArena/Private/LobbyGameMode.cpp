#include "LobbyGameMode.h"
#include "LobbyGameState.h"
#include "ShootingArenaGameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "TimerManager.h"

ALobbyGameState* ALobbyGameMode::GetLobbyGameState() const
{
	return GetGameState<ALobbyGameState>();
}

void ALobbyGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 매치에서 로비로 돌아왔는데 원래 방장이 (아직) 재접속하지 않은 경우를 대비한 안전장치.
	// 잠시 뒤에도 방장이 없으면 현재 접속자 중 첫 번째를 방장으로 지정합니다.
	GetWorldTimerManager().SetTimer(HostFallbackTimerHandle, this, &ALobbyGameMode::EnsureHostAssignedFallback, 5.0f, false);
}

void ALobbyGameMode::AssignHost(APlayerController* NewHost)
{
	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState || !NewHost || !NewHost->PlayerState)
	{
		return;
	}

	LobbyGameState->HostPlayerState = NewHost->PlayerState;
	LobbyGameState->OnLobbyStateChanged();

	if (UShootingArenaGameInstance* GameInstance = GetGameInstance<UShootingArenaGameInstance>())
	{
		GameInstance->SavedHostNetworkAddress = NewHost->PlayerState->SavedNetworkAddress;
	}

	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] AssignHost: %s (addr=%s)"),
		*NewHost->PlayerState->GetPlayerName(), *NewHost->PlayerState->SavedNetworkAddress);
}

void ALobbyGameMode::EnsureHostAssignedFallback()
{
	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState || LobbyGameState->HostPlayerState != nullptr)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (IsValid(PC) && PC->PlayerState)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] EnsureHostAssignedFallback: 원래 방장 미복귀 → %s 를 방장으로."), *PC->PlayerState->GetPlayerName());
			AssignHost(PC);
			return;
		}
	}
}

void ALobbyGameMode::InitializeLobby(const FString& DefaultMapID, int32 MaxPlayerCount)
{
	if (ALobbyGameState* LobbyGameState = GetLobbyGameState())
	{
		LobbyGameState->RebuildSlots(DefaultMapID, MaxPlayerCount);
	}
}

void ALobbyGameMode::MarkMatchLaunched()
{
	bMatchLaunchedSinceLobbyReset = true;

	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] MarkMatchLaunched: 다음 로비 복귀 시 AI 슬롯을 초기화합니다."));
}

void ALobbyGameMode::TravelToMatch(const FString& MatchTravelURL)
{
	UWorld* World = GetWorld();
	if (!World || MatchTravelURL.IsEmpty())
	{
		return;
	}

	// 방장이 "시작"을 연타하거나, 이미 다른 경로로 트래블이 예약된 경우 중복 실행을 막습니다.
	// ServerTravel 이 예약되면 World->NextURL 이 채워집니다.
	if (!World->NextURL.IsEmpty() || World->IsInSeamlessTravel())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Lobby] TravelToMatch 무시: 이미 트래블이 예약되어 있습니다. (NextURL=%s)"), *World->NextURL);
		return;
	}

	// bAbsolute=true: 로비를 열 때 붙었던 "?Game=BP_LobbyGameMode" 등 이전 URL 옵션을
	// 이어받지 않고, MatchTravelURL 에 명시된 GameMode/옵션으로만 매치 맵을 엽니다.
	UE_LOG(LogTemp, Warning, TEXT("[Lobby] TravelToMatch: %s"), *MatchTravelURL);
	World->ServerTravel(MatchTravelURL, /*bAbsolute=*/true);
}

void ALobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	if (!LobbyGameState || !NewPlayer || !NewPlayer->PlayerState)
	{
		return;
	}

	// 매치가 한 번 나간 뒤 처음 (재)접속한 플레이어라면, 지난 세션에 방장이 세팅해둔
	// AI 슬롯을 먼저 비웁니다. (로비 서버는 세션 내내 살아있어서 Slots 배열이 유지되므로,
	// 여기서 안 비우면 다음 매치의 AI 수에 계속 누적됩니다.)
	if (bMatchLaunchedSinceLobbyReset)
	{
		bMatchLaunchedSinceLobbyReset = false;
		LobbyGameState->ResetNonPlayerSlots();
	}

	LobbyGameState->AssignPlayerToOpenSlot(NewPlayer->PlayerState);

	// 매치 서버 주소를 초기화합니다. 이전 매치가 끝나고 돌아온 플레이어가 "새로" 로비에 접속하면
	// (같은 프로세스 안에서 계속 있던 게 아니라 open으로 재접속한 거라) GameState를 처음 받는 셈이라,
	// MatchServerAddress 값이 이전 매치 그대로 "7778" 등으로 남아있으면 그 초기 리플리케이션만으로도
	// OnRep_MatchServerAddress가 발동해서 방금 나온 매치 서버로 다시 끌려가버리는 버그가 있었습니다.
	// 누군가 로비에 (재)접속하는 시점엔 이미 이전 매치는 완전히 끝난 뒤이므로 안전하게 비워줍니다.
	if (!LobbyGameState->MatchServerAddress.IsEmpty())
	{
		LobbyGameState->MatchServerAddress.Empty();
		// AI 슬롯 초기화의 보조 경로. (주 경로는 위의 bMatchLaunchedSinceLobbyReset)
		LobbyGameState->ResetNonPlayerSlots();
	}

	// 방장 지정/복원. 매치를 다녀와 LobbyGameState 가 새로 생성되면 HostPlayerState 가 null 이므로,
	// 재접속 순서와 무관하게 "원래 방장"을 다시 방장으로 만들어야 합니다.
	if (LobbyGameState->HostPlayerState == nullptr)
	{
		const UShootingArenaGameInstance* GameInstance = GetGameInstance<UShootingArenaGameInstance>();
		const FString SavedHostAddr = GameInstance ? GameInstance->SavedHostNetworkAddress : FString();
		const FString ThisAddr = NewPlayer->PlayerState->SavedNetworkAddress;

		if (SavedHostAddr.IsEmpty() || (!ThisAddr.IsEmpty() && SavedHostAddr == ThisAddr))
		{
			// 저장된 방장이 아직 없거나(최초 로비), 이 플레이어가 저장된 방장 본인이면 방장으로.
			AssignHost(NewPlayer);
		}
		// else: 저장된 방장이 따로 있고 이 사람은 아님 → 방장 재접속 대기 (안 오면 BeginPlay 의 fallback 타이머가 처리).
	}
}

void ALobbyGameMode::Logout(AController* Exiting)
{
	ALobbyGameState* LobbyGameState = GetLobbyGameState();
	APlayerState* ExitingPlayerState = Exiting ? Exiting->PlayerState : nullptr;

	// [DEBUG] 임시 로그 - 문제 해결되면 제거 예정
	UE_LOG(LogTemp, Warning, TEXT("[LobbyDebug] Logout called. Exiting=%s LobbyGameState=%s ExitingPlayerState=%s"),
		Exiting ? *Exiting->GetName() : TEXT("NULL"),
		LobbyGameState ? TEXT("valid") : TEXT("NULL"),
		ExitingPlayerState ? *ExitingPlayerState->GetPlayerName() : TEXT("NULL"));

	if (LobbyGameState && ExitingPlayerState)
	{
		LobbyGameState->ReleasePlayerSlot(ExitingPlayerState);

		if (LobbyGameState->HostPlayerState == ExitingPlayerState)
		{
			// 남아있는 플레이어 중 가장 앞 슬롯을 차지한 사람에게 방장을 넘깁니다.
			APlayerState* NextHost = nullptr;
			for (const FLobbySlot& Slot : LobbyGameState->Slots)
			{
				if (Slot.SlotType == ELobbySlotType::Player && Slot.OwningPlayerState)
				{
					NextHost = Slot.OwningPlayerState;
					break;
				}
			}

			LobbyGameState->HostPlayerState = NextHost;
			LobbyGameState->OnLobbyStateChanged();

			// 저장된 방장 주소도 새 방장으로 갱신 (원래 방장이 나갔으므로).
			if (UShootingArenaGameInstance* GameInstance = GetGameInstance<UShootingArenaGameInstance>())
			{
				GameInstance->SavedHostNetworkAddress = NextHost ? NextHost->SavedNetworkAddress : FString();
			}
		}
	}

	Super::Logout(Exiting);
}
