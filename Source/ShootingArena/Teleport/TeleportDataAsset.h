#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TeleportDataAsset.generated.h"

class USoundBase;

/**
 * 단방향 텔레포트의 기획 수치만 보관하는 DataAsset입니다.
 * 실제 텔레포트 로직은 AOneWayTeleportActor가 담당합니다.
 */
UCLASS(BlueprintType)
class SHOOTINGARENA_API UTeleportDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 출구 정면을 0도로 보고 위쪽으로 올리는 발사 각도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teleport",
		meta = (ClampMin = "0", ClampMax = "360", UIMin = "0", UIMax = "360"))
	int32 launchAngle = 15;

	/** 출구에서 캐릭터를 발사하는 세기입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teleport",
		meta = (ClampMin = "0.0"))
	float launchPower = 600.0f;

	/** 발사 직후 이동/점프 입력을 제한하는 시간입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teleport",
		meta = (ClampMin = "0.0"))
	float moveLockTime = 0.2f;

	/** 포탈 이용자 본인에게만 재생할 사운드입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teleport|Sound")
	TObjectPtr<USoundBase> teleportSound;

	/** 사운드 에셋 기본 볼륨에 곱할 값입니다. 0이면 무음, 1이면 원본 볼륨입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teleport|Sound",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float soundVolumeMultiplier = 1.0f;
};
