
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

public:
	// 이 레벨이 열릴 때 붙은 URL 옵션 전체를 반환합니다 (예: "?AIEasy=2?AINormal=1?AIHard=0").
	// AGameModeBase::OptionsString은 protected라서 블루프린트에서 못 읽는데,
	// 매치 서버(BP_MultiplayerAIGameMode)가 스폰될 때 맵 이름 뒤에 붙여 넘긴
	// ?AIEasy=/?AINormal=/?AIHard= 값을 블루프린트에서 Parse Option으로 읽으려면 필요합니다.
	UFUNCTION(BlueprintPure, Category = "Server")
	FString GetLevelOptionsString() const { return OptionsString; }
};
