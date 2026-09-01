
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
};
