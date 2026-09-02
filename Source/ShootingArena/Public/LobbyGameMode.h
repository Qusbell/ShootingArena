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

	// Server_StartGame이 OnLobbyStartGameApproved(=AI 수 URL 옵션 계산 + 매치 서버 스폰)를
	// 마친 직후 호출합니다. "이번 로비 세션에서 매치가 한 번 나갔다"를 기록해두고, 다음에
	// 플레이어가 매치에서 로비로 돌아오면(PostLogin) 지난 세션의 AI 슬롯을 비웁니다.
	// 이렇게 안 하면 로비 서버는 계속 살아있어서 AI 슬롯이 그대로 남고, 다음 매치의 AI 수에
	// 계속 누적됩니다(1 -> 2 -> 3 ...).
	void MarkMatchLaunched();

private:
	ALobbyGameState* GetLobbyGameState() const;

	// 위 MarkMatchLaunched 참고. 매치가 나간 뒤 첫 번째 (재)접속에서 소비되고 false로 돌아갑니다.
	bool bMatchLaunchedSinceLobbyReset = false;
};
