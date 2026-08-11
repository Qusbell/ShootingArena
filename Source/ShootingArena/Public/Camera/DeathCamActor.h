#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "DeathCamActor.generated.h"

class APlayerController;
class UDeathCamDataAsset;

/**
 * 실제 DeathCam 시점을 담당하는 CameraActor입니다.
 *
 * 기본 상태에서는 Death Location을 화면 중앙에 고정하고,
 * Other 방향에 대해 약 90도 옆에서 바라보는 Side View 구도를 만듭니다.
 * Other를 해당 구도 안에 담을 수 없거나 벽에 가려지는 경우에만 Top View로 전환합니다.
 */
UCLASS(Blueprintable)
class SHOOTINGARENA_API ADeathCamActor : public ACameraActor
{
	GENERATED_BODY()

public:
	ADeathCamActor();

	virtual void Tick(float deltaSeconds) override;

	/** 사망 위치를 고정 기준점으로 저장하고 첫 DeathCam 구도를 즉시 계산합니다. */
	bool InitializeDeathCam(
		const FVector& inDeathLocation,
		AActor* inOtherActor,
		APlayerController* inPlayerController,
		UDeathCamDataAsset* inDeathCamData,
		AActor* inDeadActorToIgnore);

	/** 카메라 갱신 Tick만 중지합니다. ViewTarget 복귀/Destroy는 DeathCamComponent가 담당합니다. */
	void StopDeathCam();

	UFUNCTION(BlueprintPure, Category = "DeathCam")
	bool IsDeathCamActive() const { return bDeathCamActive; }

	UFUNCTION(BlueprintPure, Category = "DeathCam")
	bool IsTopView() const { return bTopView; }

	UFUNCTION(BlueprintPure, Category = "DeathCam")
	FVector GetDeathLocation() const { return deathLocation; }

private:
	UPROPERTY(Transient)
	TObjectPtr<AActor> otherActor;

	UPROPERTY(Transient)
	TObjectPtr<AActor> deadActorToIgnore;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> ownerPlayerController;

	FVector deathLocation = FVector::ZeroVector;
	FRotator topViewTargetRotation = FRotator::ZeroRotator;

	bool bDeathCamActive = false;
	bool bTopView = false;

	// DataAsset 값을 런타임에 복사합니다.
	// DataAsset이 비어 있어도 아래 기본값으로 동작합니다.
	float orbitDistance = 600.0f;
	float orbitPitch = -20.0f;
	float orbitYawOffset = 0.0f;
	float orbitRotationSpeed = 90.0f;
	float topViewDistance = 800.0f;
	float topViewPitch = -90.0f;
	float topViewYawOffset = 0.0f;
	float topViewMoveSpeed = 500.0f;
	float topViewRotationSpeed = 90.0f;
	float orbitAlignmentTolerance = 1.0f;
	float screenSafeRatio = 0.85f;

	// 충돌로 실제 위치가 당겨져도 목표 거리는 별도로 유지합니다.
	float currentTargetDistance = 600.0f;

	// Death Location -> Other 방향 기준으로 90도 옆에서 바라보는 것이 기본 Side View입니다.
	static constexpr float sideViewBaseYawOffset = 90.0f;

	// 기존 SpringArm Camera Collision과 같은 역할을 하는 Probe 반경입니다.
	static constexpr float cameraProbeRadius = 12.0f;

	void CacheSettings(UDeathCamDataAsset* inDeathCamData);
	FRotator CalculateOrbitTargetRotation() const;
	float CalculateRequiredOrbitDistance(const FRotator& viewRotation) const;
	void UpdateOrbit(float deltaSeconds);
	void UpdateTopView(float deltaSeconds);
	void SwitchToTopView(const TCHAR* reason);

	/** 일반 Side View에서 Death Location과 목표 Camera 위치 사이의 충돌을 처리합니다. */
	FVector ResolveCameraLocation(const FRotator& viewRotation, float targetDistance) const;

	/** Top View에서 Death Location 바로 위 목표 위치까지의 충돌을 처리합니다. */
	FVector ResolveTopViewLocation(float targetDistance) const;

	/** 일반 Side View의 CameraActor World Transform을 적용합니다. */
	void ApplyCameraTransform(const FRotator& viewRotation, float targetDistance);

	bool HasClearViewToOther() const;

	/** 현재 DeathCam 자체의 카메라 기준으로 Other가 실제 화면 프레임 안에 들어오는지 계산합니다. */
	bool IsOtherInsideScreen() const;

	bool HasReachedOrbitRotation(const FRotator& targetRotation) const;
};
