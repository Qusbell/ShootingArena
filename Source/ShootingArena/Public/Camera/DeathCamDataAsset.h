#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialInterface.h"
#include "DeathCamDataAsset.generated.h"

/**
 * DeathCam에서 디자이너가 조절할 값만 보관하는 DataAsset입니다.
 * 게임 로직/네트워크 로직은 이 클래스에 넣지 않습니다.
 *
 * 코드만 먼저 병합해도 기존 DataAsset/Blueprint가 깨지지 않도록
 * 이전 SideView/TopView 변수는 Legacy 호환용으로 잠시 유지합니다.
 */
UCLASS(BlueprintType)
class SHOOTINGARENA_API UDeathCamDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------------
	// Final DeathCam Design
	// ---------------------------------------------------------------------

	/**
	 * 최종 기획의 Offset.
	 * 기획서 자료형이 Float이므로 사망 위치에 World Z 방향으로 더하는 높이 값으로 사용합니다.
	 * Center = DeathLocation + UpVector * centerOffset
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Camera")
	float centerOffset = 0.0f;

	/**
	 * DeathCam 진입 시 Center 반대편에 만드는 초기 카메라 거리입니다.
	 * 이 초기 위치와 Center 사이 거리가 런타임 MaxDistance가 됩니다.
	 *
	 * 0이면 코드-only migration을 위해 기존 orbitDistance 값을 대신 사용합니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Camera", meta = (ClampMin = "0.0"))
	float initialCameraDistance = 0.0f;

	/** Killer 이동으로 기본 위치가 바뀔 때 카메라가 해당 위치로 보간되는 속도입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Camera", meta = (ClampMin = "0.0"))
	float cameraMoveInterpSpeed = 5.0f;

	/** Killer CustomDepth/Stencil 강조 사용 여부. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|KillerHighlight")
	bool bEnableKillerHighlight = true;

	/**
	 * DeathCam 카메라에 적용할 Post Process Material입니다.
	 * CustomStencil == killerHighlightStencilValue 인 픽셀을 붉게 표시하는 Material을 지정합니다.
	 * 비어 있으면 카메라/Stencil 로직은 정상 동작하지만 화면의 붉은 강조 효과는 표시되지 않습니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|KillerHighlight")
	TObjectPtr<UMaterialInterface> killerHighlightMaterial = nullptr;

	/**
	 * Killer Highlight에 사용할 Custom Stencil 값입니다.
	 * 이 값은 기존 코드에 이미 존재하므로 중복 선언하지 않습니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|KillerHighlight", meta = (ClampMin = "0", ClampMax = "255"))
	int32 killerHighlightStencilValue = 1;

	/** 부활 후 DeathCam에서 새 Pawn 카메라로 돌아갈 때의 ViewTarget Blend 시간입니다. DeathCam 진입은 즉시 전환됩니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Transition", meta = (ClampMin = "0.0"))
	float viewTargetBlendTime = 0.2f;

	// ---------------------------------------------------------------------
	// Legacy Compatibility
	// 기존 BP/DataAsset이 이 프로퍼티를 가지고 있어도 코드-only PR에서 깨지지 않게 유지합니다.
	// 새 최종 로직에서는 orbitDistance만 initialCameraDistance의 migration fallback으로 사용합니다.
	// 나머지는 사용하지 않습니다.
	// ---------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (ClampMin = "0.0", DeprecatedProperty, DeprecationMessage = "Use initialCameraDistance. This value is only a migration fallback when initialCameraDistance is 0."))
	float orbitDistance = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (ClampMin = "-89.9", ClampMax = "89.9", DeprecatedProperty, DeprecationMessage = "SideView orbit pitch was removed from the final DeathCam design."))
	float orbitPitch = -20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "SideView yaw offset was removed from the final DeathCam design."))
	float orbitYawOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (ClampMin = "0.0", DeprecatedProperty, DeprecationMessage = "Orbit rotation was removed from the final DeathCam design."))
	float orbitRotationSpeed = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (ClampMin = "0.0", DeprecatedProperty, DeprecationMessage = "TopView was removed from the final DeathCam design."))
	float topViewDistance = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (ClampMin = "-90.0", ClampMax = "89.9", DeprecatedProperty, DeprecationMessage = "TopView was removed from the final DeathCam design."))
	float topViewPitch = -90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (DeprecatedProperty, DeprecationMessage = "TopView was removed from the final DeathCam design."))
	float topViewYawOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (ClampMin = "0.0", DeprecatedProperty, DeprecationMessage = "TopView was removed from the final DeathCam design."))
	float topViewMoveSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (ClampMin = "0.0", DeprecatedProperty, DeprecationMessage = "TopView was removed from the final DeathCam design."))
	float topViewRotationSpeed = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (ClampMin = "0.10", ClampMax = "0.98", DeprecatedProperty, DeprecationMessage = "Screen framing logic was removed from the final DeathCam design."))
	float screenSafeRatio = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DeathCam|Legacy", meta = (ClampMin = "0.01", ClampMax = "10.0", DeprecatedProperty, DeprecationMessage = "Orbit alignment logic was removed from the final DeathCam design."))
	float orbitAlignmentTolerance = 1.0f;
};
