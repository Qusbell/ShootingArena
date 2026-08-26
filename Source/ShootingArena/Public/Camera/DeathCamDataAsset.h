#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialInterface.h"
#include "DeathCamDataAsset.generated.h"

/**
 * DeathCam에서 디자이너가 조절할 값만 보관하는 DataAsset입니다.
 * 게임 로직/네트워크 로직은 이 클래스에 넣지 않습니다.
 *
 * 최종 기획에서 실제로 사용하는 값만 유지합니다.
 */
UCLASS(BlueprintType)
class SHOOTINGARENA_API UDeathCamDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------------
	// Camera
	// ---------------------------------------------------------------------

	/**
	 * 최종 기획의 Offset.
	 * 기획서 자료형이 Float이므로 사망 위치에 World Z 방향으로 더하는 높이 값으로 사용합니다.
	 * Center = DeathLocation + UpVector * centerOffset
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Camera")
	float centerOffset = 70.0f;

	/**
	 * DeathCam 진입 시 Center 반대편에 만드는 초기 카메라 거리입니다.
	 * 이 초기 위치와 Center 사이 거리가 런타임 MaxDistance가 됩니다.
	 *
	 * 기존 DataAsset이 이미 0으로 저장되어 있는 경우를 대비해,
	 * 런타임에서는 0 이하일 때 안전 기본값 600을 사용합니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Camera", meta = (ClampMin = "0.0"))
	float initialCameraDistance = 600.0f;

	/** Killer 이동으로 기본 위치가 바뀔 때 카메라가 해당 위치로 보간되는 속도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Camera", meta = (ClampMin = "0.0"))
	float cameraMoveInterpSpeed = 5.0f;

	// ---------------------------------------------------------------------
	// Killer Highlight
	// ---------------------------------------------------------------------

	/** Killer CustomDepth/Stencil 강조 사용 여부. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|KillerHighlight")
	bool bEnableKillerHighlight = true;

	/**
	 * DeathCam 카메라에 적용할 Post Process Material입니다.
	 * CustomStencil == killerHighlightStencilValue 인 픽셀을 붉게 표시하는 Material을 지정합니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|KillerHighlight")
	TObjectPtr<UMaterialInterface> killerHighlightMaterial = nullptr;

	/** Killer Highlight에 사용할 Custom Stencil 값입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|KillerHighlight", meta = (ClampMin = "0", ClampMax = "255"))
	int32 killerHighlightStencilValue = 1;

	// ---------------------------------------------------------------------
	// Transition
	// ---------------------------------------------------------------------

	/** 부활 후 DeathCam에서 새 Pawn 카메라로 돌아갈 때의 ViewTarget Blend 시간입니다. DeathCam 진입은 즉시 전환됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Transition", meta = (ClampMin = "0.0"))
	float viewTargetBlendTime = 0.2f;
};
