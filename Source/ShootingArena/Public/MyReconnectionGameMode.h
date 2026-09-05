
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MyReconnectionGameMode.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UReconnectionInterface : public UInterface { GENERATED_BODY() };

class IReconnectionInterface
{
	GENERATED_BODY()

public:
	// ��������Ʈ���� �� �̺�Ʈ�� �����Ͽ� ��ū�� ������ �����ϵ��� �մϴ�.
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Reconnection")
	void SetConnectionToken(const FString& Token);

	// ��������Ʈ���� ����� ��ū�� C++ ���Ӹ�忡�� �ٽ� ������ �ֵ��� �մϴ�.
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Reconnection")
	FString GetConnectionToken() const;
};

/**
 * InitNewPlayer�� ����ä�� ���Ӱ� �Բ� ���� Token�� �����ϴ� GameMode
 */
UCLASS()
class SHOOTINGARENA_API AMyReconnectionGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
protected:

	// �α��� ������ ��ū�� �Ľ��ؼ� ��Ʈ�ѷ����� �������̽��� ������
	virtual FString InitNewPlayer(APlayerController* NewPlayerController,
		const FUniqueNetIdRepl& UniqueId,
		const FString& Options,
		const FString& Portal) override;

	// AGameModeBase 는 AGameMode 와 달리 PostLogin 에서 PlayerState::SavedNetworkAddress 를
	// 채워주지 않습니다. ServerTravel(같은 프로세스) 후 GameInstance 로 닉네임을 재연결하려면
	// 이 키가 필요하므로 여기서 직접 채우고, 저장된 닉네임이 있으면 적용합니다.
	// (InitNewPlayer 시점엔 아직 PC 에 NetConnection 이 안 붙어 주소를 못 구합니다.)
	virtual void PostLogin(APlayerController* NewPlayer) override;

	// 플레이어가 나가면 "로비로" 투표 현황(전원 투표 여부)이 바뀔 수 있으므로 재확인합니다.
	// PlayerState가 PlayerArray에서 실제로 빠지는 건 이 함수가 끝난 다음이라 다음 틱에 확인합니다.
	virtual void Logout(AController* Exiting) override;

public:
	// 매치에 접속한 모든 인간 플레이어(AI 제외)가 "로비로"에 투표했는지 확인하고,
	// 그렇다면(그리고 1명 이상 접속해 있다면) 로비로 collective ServerTravel 합니다.
	// AMyPlayerState::Server_SetWantsReturnToLobby 와 Logout 에서 호출됩니다.
	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void CheckReturnToLobbyVotes();

	// 이 레벨이 열릴 때 붙은 URL 옵션 전체를 반환합니다 (예: "?AIEasy=2?AINormal=1?AIHard=0").
	// AGameModeBase::OptionsString은 protected라서 블루프린트에서 못 읽는데,
	// 매치 서버(BP_MultiplayerAIGameMode)가 스폰될 때 맵 이름 뒤에 붙여 넘긴
	// ?AIEasy=/?AINormal=/?AIHard= 값을 블루프린트에서 Parse Option으로 읽으려면 필요합니다.
	UFUNCTION(BlueprintPure, Category = "Server")
	FString GetLevelOptionsString() const { return OptionsString; }
};
