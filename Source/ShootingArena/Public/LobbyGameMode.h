#pragma once

#include "CoreMinimal.h"
#include "MyReconnectionGameMode.h"
#include "LobbyGameMode.generated.h"

class ALobbyGameState;

/**
 * 대기 로비(LobbyLevel) 전용 GameMode.
 * 접속/퇴장 시 슬롯 배정과 방장 승계를 처리합니다.
 *
 * AMyReconnectionGameMode를 상속해서, 매치 서버로 접속할 때와 마찬가지로
 * "open IP:7777?Nickname=..." 로 넘어오는 라이브 닉네임을 InitNewPlayer 단계에서
 * 정상적으로 파싱/적용받습니다 (안 그러면 기본값인 "?Name=컴퓨터이름"으로 덮어써집니다).
 */
UCLASS()
class SHOOTINGARENA_API ALobbyGameMode : public AMyReconnectionGameMode
{
	GENERATED_BODY()

public:
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	// 서버가 로비 레벨을 시작할 때 (BP_LobbyGameMode의 BeginPlay 등에서) 최초 1회 호출해서
	// 기본 맵과 최대 인원수로 슬롯을 구성합니다. MaxPlayerCount는 BP가 DataTable에서 조회해 넘겨줍니다.
	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void InitializeLobby(const FString& DefaultMapID, int32 MaxPlayerCount);

	// ALobbyPlayerController::Server_StartGame이 조건(방장 & 전원 준비완료)을 통과하면 호출됩니다.
	// 실제 맵 에셋 조회(DataTable) 및 레벨 이동은 BP에서 구현합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Lobby")
	void OnLobbyStartGameApproved(const FString& SelectedMapID);

private:
	ALobbyGameState* GetLobbyGameState() const;
};
