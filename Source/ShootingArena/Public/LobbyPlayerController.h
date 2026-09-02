#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LobbyTypes.h"
#include "LobbyPlayerController.generated.h"

class ALobbyGameState;

/**
 * 대기 로비(LobbyLevel)에서 클라이언트가 서버에 요청을 보낼 때 쓰는 PlayerController.
 * 방장 전용 RPC들은 내부적으로 IsHost() 체크를 하기 때문에, 방장이 아닌 클라이언트가 호출해도 무시됩니다.
 */
UCLASS()
class SHOOTINGARENA_API ALobbyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// 방장 전용: 슬롯 상태를 전환합니다 (열림 <-> 잠금 <-> AI).
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void Server_SetSlotType(int32 SlotIndex, ELobbySlotType NewType);

	// 방장 전용: AI 슬롯의 이름/난이도를 설정합니다. 대상 슬롯이 비어있으면 자동으로 AI 타입으로 바뀝니다.
	// 설정이 끝나면 해당 슬롯은 자동으로 준비완료 처리됩니다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void Server_SetAIConfig(int32 SlotIndex, const FString& AIName, ELobbyDifficulty Difficulty);

	// 본인이 차지한 슬롯의 준비 상태를 설정합니다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void Server_SetReady(bool bReady);

	// 본인의 표시 이름(닉네임)을 설정합니다. PlayerState와, 본인이 차지한 슬롯의 DisplayName을 함께 갱신합니다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void Server_SetPlayerName(const FString& NewName);

	// 방장 전용: 선택된 맵을 바꿉니다. NewMaxPlayerCount는 호출하는 쪽(BP)이 DataTable에서 조회해서 넘겨줍니다.
	// 현재 채워진 슬롯 수가 NewMaxPlayerCount보다 많으면 무시됩니다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void Server_ChangeMap(const FString& NewMapID, int32 NewMaxPlayerCount);

	// 방장 전용: 방장을 제외한 모든 플레이어가 준비 완료 상태면 GameMode에 게임 시작을 요청합니다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void Server_StartGame();

	// 방장 전용: 로비 서버가 내부적으로 띄워둔 매치 서버(있다면)를 정리합니다.
	// 방장이 "메인메뉴로" 나가면서 자기 클라이언트에서 로비 서버 프로세스 자체를 강제 종료(TerminateProc)하는데,
	// 그 안에서 띄운 매치 서버 자식 프로세스는 부모가 죽어도 자동으로 안 죽고 고아 프로세스로 남습니다.
	// 그래서 로비 프로세스를 끄기 "직전에" 서버(=로비 프로세스) 스스로에게 먼저 자기 매치 서버를 정리하라고
	// 시켜야 합니다. 방장이 아니면 애초에 로비 프로세스를 자기가 띄운 게 아니라서 아무 효과가 없습니다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void Server_CleanupMatchServer();

private:
	ALobbyGameState* GetLobbyGameState() const;
	bool IsHost() const;
};
