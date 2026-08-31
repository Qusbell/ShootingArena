#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "DeathCamActor.generated.h"

class APlayerController;
class UDeathCamDataAsset;
class UPrimitiveComponent;
class UMaterialInterface;

/**
 * 실제 DeathCam 시점을 담당하는 CameraActor입니다.
 *
 * 최종 기획 기준:
 * - 기준점(Center) = 사망 위치 + Z Offset
 * - 기본 관계는 Camera - Center - Killer가 같은 선상
 * - Camera는 Center를 기준으로 Killer의 반대편에 위치
 * - Killer 이동 시 새 기본 위치를 계산하고 부드럽게 이동
 * - 지형 충돌 시 Hit Normal을 기준으로 벽면을 따라 Slide
 * - Camera는 항상 Center를 바라봄
 *
 * 네트워크 / ViewTarget / Spawn / Destroy는 DeathCamComponent가 담당합니다.
 */
UCLASS(Blueprintable)
class SHOOTINGARENA_API ADeathCamActor : public ACameraActor
{
	GENERATED_BODY()

public:
	ADeathCamActor();

	virtual void Tick(float deltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

	/**
	 * 기존 DeathCamComponent에서 사용하던 시그니처를 그대로 유지합니다.
	 * PlayerController Blueprint를 수정하지 않아도 기존 StartDeathCam 흐름이 깨지지 않게 하기 위함입니다.
	 */
	bool InitializeDeathCam(
		const FVector& inDeathLocation,
		AActor* inOtherActor,
		APlayerController* inPlayerController,
		UDeathCamDataAsset* inDeathCamData,
		AActor* inDeadActorToIgnore);

	/** 카메라 갱신과 Killer Highlight를 중지합니다. ViewTarget 복귀/Destroy는 DeathCamComponent가 담당합니다. */
	void StopDeathCam();

	UFUNCTION(BlueprintPure, Category = "DeathCam")
	bool IsDeathCamActive() const { return bDeathCamActive; }

	/**
	 * TopView는 최종 기획에서 제거되었습니다.
	 * 기존 Blueprint가 이 함수를 참조하고 있어도 컴파일 오류가 나지 않도록 호환용으로 남겨두며 항상 false를 반환합니다.
	 */
	UFUNCTION(BlueprintPure, Category = "DeathCam", meta = (DeprecatedFunction, DeprecationMessage = "TopView was removed from the final DeathCam design."))
	bool IsTopView() const { return false; }

	UFUNCTION(BlueprintPure, Category = "DeathCam")
	FVector GetDeathLocation() const { return deathLocation; }

	/** 최종 기획의 기준점: DeathLocation + Offset(Z). */
	UFUNCTION(BlueprintPure, Category = "DeathCam")
	FVector GetCenterLocation() const { return centerLocation; }

	/** DeathCam 진입 시 확정되고 유지되는 최대 카메라 거리. */
	UFUNCTION(BlueprintPure, Category = "DeathCam")
	float GetMaxDistance() const { return maxDistance; }

private:
	/** 최종 기획에서 말하는 Killer. 기존 코드/호환을 위해 입력 이름은 OtherActor를 유지합니다. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> otherActor;

	UPROPERTY(Transient)
	TObjectPtr<AActor> deadActorToIgnore;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> ownerPlayerController;

	/** 실제 사망 위치. */
	FVector deathLocation = FVector::ZeroVector;

	/** 기준점 = deathLocation + FVector::UpVector * centerOffset. */
	FVector centerLocation = FVector::ZeroVector;

	/** DeathCam 시작 시 정해지고 이후 증가하지 않는 최대 이동 거리. */
	float maxDistance = 600.0f;

	bool bDeathCamActive = false;

	// DataAsset 값을 런타임에 복사합니다.
	// DataAsset이 비어 있어도 아래 기본값으로 안전하게 동작합니다.
	float centerOffset = 0.0f;
	float initialCameraDistance = 600.0f;
	float cameraMoveInterpSpeed = 5.0f;
	bool bEnableKillerHighlight = true;
	int32 killerHighlightStencilValue = 1;

	/** DeathCam 카메라에만 적용할 Killer Highlight Post Process Material. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> killerHighlightMaterial;

	// 기존 SpringArm Camera Collision과 같은 역할을 하는 Probe 반경입니다.
	static constexpr float cameraProbeRadius = 12.0f;

	// Sweep가 벽에 정확히 맞닿은 지점에서 다음 Sweep을 시작하면
	// 같은 벽을 Time=0으로 다시 맞아 Slide가 뻑뻑해질 수 있습니다.
	// 충돌 후 카메라를 표면에서 아주 조금 떼어 두는 기술적 여유값입니다.
	static constexpr float cameraCollisionSkin = 2.0f;

	// 모서리처럼 한 프레임에 두 면 이상을 만나는 경우에도
	// 남은 이동량을 다시 투영할 수 있도록 제한된 횟수만 반복합니다.
	static constexpr int32 maxSlideIterations = 3;

	/** Killer Highlight 종료 시 원래 CustomDepth 상태로 되돌리기 위한 백업입니다. */
	struct FHighlightComponentState
	{
		TWeakObjectPtr<UPrimitiveComponent> component;
		bool bRenderCustomDepth = false;
		int32 customDepthStencilValue = 0;
	};

	TArray<FHighlightComponentState> killerHighlightStates;

	void CacheSettings(UDeathCamDataAsset* inDeathCamData);
	void UpdateDeathCam(float deltaSeconds);

	/** Center -> Killer 방향. Killer가 유효하지 않으면 안전한 fallback 방향을 반환합니다. */
	FVector CalculateKillerDirection() const;

	/** 기획의 기본 Camera 위치: Center - KillerDirection * MaxDistance. */
	FVector CalculateBaseCameraLocation(const FVector& killerDirection) const;

	/** DeathCam 첫 프레임 위치를 지형 안에 생성하지 않도록 Center -> 초기 위치 Sweep을 수행합니다. */
	FVector ResolveInitialCameraLocation(const FVector& desiredCameraLocation) const;

	/** 현재 위치에서 원하는 이동량만큼 움직이되, 충돌하면 Hit Normal 기준으로 벽면 Slide를 적용합니다. */
	FVector MoveCameraWithCollision(const FVector& cameraMove, const FVector& killerDirection) const;

	/** MaxDistance와 'Center를 넘어 Killer 방향으로 이동 금지' 규칙을 마지막에 강제합니다. */
	FVector ClampCameraLocation(const FVector& candidateLocation, const FVector& killerDirection) const;

	/** Camera가 어느 위치에 있든 Center를 바라보도록 회전을 갱신합니다. */
	FRotator CalculateLookAtCenterRotation(const FVector& cameraLocation) const;

	/** DataAsset의 Highlight Type에 따라 선택된 Post Process Material을 이 DeathCam 카메라에만 적용합니다. */
	void ApplyKillerHighlightPostProcess();

	void ApplyKillerHighlight();
	void RemoveKillerHighlight();
};
