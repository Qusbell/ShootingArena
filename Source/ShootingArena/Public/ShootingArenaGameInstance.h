#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "LobbyTypes.h"
#include "ShootingArenaGameInstance.generated.h"

/**
 * 프로젝트 전역 GameInstance. 레벨 이동(비-심리스 트래블) 중에도 서버 프로세스에서 계속 살아있으므로,
 * "같은 접속을 유지한 플레이어"에게 레벨이 바뀐 뒤에도 계속 적용되어야 하는 값(예: 닉네임)을 여기 저장합니다.
 * 플레이어 식별은 PlayerState::SavedNetworkAddress(엔진이 접속 시 자동으로 채워주는, 재접속 매칭용 주소 문자열)를 키로 사용합니다.
 */
UCLASS()
class SHOOTINGARENA_API UShootingArenaGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// NetworkAddress(PlayerState::SavedNetworkAddress)를 키로 닉네임을 저장합니다. 서버 전용입니다.
	UFUNCTION(BlueprintCallable, Category = "Player")
	void SetSavedNickname(const FString& NetworkAddress, const FString& Nickname);

	// 저장된 닉네임을 찾습니다. 없으면 빈 문자열을 반환합니다. 서버 전용입니다.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Player")
	FString GetSavedNickname(const FString& NetworkAddress) const;

	// 로비에서 설정한 AI 슬롯들의 난이도 목록입니다. 방장이 게임 시작을 누르면
	// BP_LobbyGameMode가 LobbyGameState->Slots(AI 타입)에서 이 배열을 채우고,
	// 매치 레벨의 GameMode(예: BP_MultiplayerAIGameMode)가 레벨 이동 후 이 값을 읽어
	// 그 개수/난이도만큼 AI를 스폰합니다. 서버 전용입니다.
	UPROPERTY(BlueprintReadWrite, Category = "Lobby")
	TArray<ELobbyDifficulty> PendingAIDifficulties;

private:
	UPROPERTY()
	TMap<FString, FString> NicknameByNetworkAddress;
};
