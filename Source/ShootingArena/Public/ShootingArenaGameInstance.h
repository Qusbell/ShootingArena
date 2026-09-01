#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
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

private:
	UPROPERTY()
	TMap<FString, FString> NicknameByNetworkAddress;
};
