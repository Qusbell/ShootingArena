#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DeathCamDataAsset.generated.h"

/**
 * DeathCam에서 디자이너가 조절할 값만 보관하는 DataAsset입니다.
 * 게임 로직/네트워크 로직은 이 클래스에 넣지 않습니다.
 */
UCLASS(BlueprintType)
class SHOOTINGARENA_API UDeathCamDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 기본 Side View 상태에서 Death Location과 카메라 사이의 목표 거리입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Orbit", meta = (ClampMin = "0.0"))
	float orbitDistance = 600.0f;

	/** 기본 Side View 상태에서 Death Location을 바라보는 카메라의 상하 각도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Orbit", meta = (ClampMin = "-89.9", ClampMax = "89.9"))
	float orbitPitch = -20.0f;

	/** 기본 90도 Side View에서 추가로 적용할 좌우 각도 보정값입니다. 0이면 정확한 Side View입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Orbit")
	float orbitYawOffset = 0.0f;

	/** Other 이동에 따라 Side View 카메라가 Death Location을 중심으로 따라가는 회전 속도(Degrees / Second)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Orbit", meta = (ClampMin = "0.0"))
	float orbitRotationSpeed = 90.0f;

	/** Top View 상태에서 Death Location과 카메라 사이의 목표 높이입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|TopView", meta = (ClampMin = "0.0"))
	float topViewDistance = 800.0f;

	/** Top View의 상하 각도입니다. -90이면 수직 위에서 Death Location을 내려다봅니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|TopView", meta = (ClampMin = "-90.0", ClampMax = "89.9"))
	float topViewPitch = -90.0f;

	/** Top View 전환 순간의 Yaw에 추가할 고정 보정값입니다. Top View 진입 후 Other를 따라 회전하지 않습니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|TopView")
	float topViewYawOffset = 0.0f;

	/** Side View 위치에서 Death Location 위 Top View 위치로 이동하는 속도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|TopView", meta = (ClampMin = "0.0"))
	float topViewMoveSpeed = 500.0f;

	/** Top View 목표 각도까지 회전하는 속도(Degrees / Second)입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|TopView", meta = (ClampMin = "0.0"))
	float topViewRotationSpeed = 90.0f;


	/**
	 * Side View에서 Other를 화면 가장자리에 딱 붙이지 않고 안쪽에 유지하기 위한 비율입니다.
	 * 0.85면 화면 반폭/반높이의 약 85% 안쪽에 Other가 들어오도록 카메라 거리를 자동 조절합니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Framing", meta = (ClampMin = "0.10", ClampMax = "0.98"))
	float screenSafeRatio = 0.85f;

	/** 부활 후 DeathCam에서 새 Pawn 카메라로 돌아갈 때의 ViewTarget Blend 시간입니다. DeathCam 진입은 항상 즉시 전환됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Transition", meta = (ClampMin = "0.0"))
	float viewTargetBlendTime = 0.2f;

	/**
	 * 카메라가 목표 Side View 회전에 사실상 도달했는지 판단하는 기술적 오차 허용값입니다.
	 * Other의 거리/Z 높이를 제한하는 게임플레이 임계값이 아닙니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Internal", meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float orbitAlignmentTolerance = 1.0f;
};
