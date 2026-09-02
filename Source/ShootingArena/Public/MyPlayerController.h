
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MyPlayerController.generated.h"

/**
 * 
 */
UCLASS()
class SHOOTINGARENA_API AMyPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	virtual void PawnLeavingGame() override;
	

	// ������ ���߰ų� �簳�ش޶�� ������ ��û�ϴ� �Լ�.
    // Ŭ���̾�Ʈ�� �� �Լ��� ȣ���ϸ�, ���� ������ �������� ��� ����.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Pause")
	void ServerSetGamePaused(bool bShouldPause);

	// GameMode(서버)가 매치 종료 등 게임 자체의 판단으로 일시정지를 강제할 때 쓰는 함수입니다.
	// ServerSetGamePaused와 달리 인원수 제한이 없습니다 — 특정 플레이어가 남용하는 게 아니라
	// 서버 로직이 모두를 동시에 멈추는 것이라 어뷰징 문제가 없기 때문입니다.
	// GameMode에서 직접(서버 컨텍스트) 호출하는 용도이므로 Server RPC가 아닙니다.
	UFUNCTION(BlueprintCallable, Category = "Pause")
	void SetGamePausedByGameMode(bool bShouldPause);

	// 매치 결과창에서 로비로 돌아가기 버튼을 눌렀을 때 호출합니다.
	// 데디케이트 서버 전체를 Lobby_Level로 ServerTravel시켜 접속된 모든 플레이어를 함께 로비로 되돌립니다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void ServerReturnToLobby();

	// 엔진의 GetNetMode()는 블루프린트에 노출되어 있지 않아서 감싸둔 함수입니다.
	// 캠페인(로컬 단독 실행)은 항상 true, 로비를 거친 멀티플레이(데디케이트 서버 접속)는 항상 false입니다.
	UFUNCTION(BlueprintPure, Category = "Network")
	bool IsStandaloneGame() const;

	// 결과창(WBP_Result)에서 "메인메뉴로"/"로비로" 버튼을 눌러 매치 서버를 떠날 때 호출합니다.
	// 이 매치 서버 프로세스 자기 자신을 정상 종료시켜서, 로비 프로세스가 나중에 정리해주길
	// 기다리지 않고 즉시 자원을 반환합니다 (그래야 다음 매치가 포트 충돌 없이 뜹니다).
	// 캠페인은 별도로 "Stop Single Server"(GameInstance, 클라이언트 측)로 정리되므로 이 함수는
	// 호출하는 쪽(Blueprint)에서 멀티플레이 세션일 때만 부르면 됩니다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Match")
	void ServerShutdownMatchServer();
};
