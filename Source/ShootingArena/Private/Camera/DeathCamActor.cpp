#include "Camera/DeathCamActor.h"

#include "Camera/CameraComponent.h"
#include "Camera/DeathCamDataAsset.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Math/RotationMatrix.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeathCamActor, Log, All);

ADeathCamActor::ADeathCamActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// DeathCam Actor는 owning client에만 로컬 생성합니다.
	SetReplicates(false);
}

void ADeathCamActor::Tick(float deltaSeconds)
{
	Super::Tick(deltaSeconds);

	if (!bDeathCamActive)
	{
		return;
	}

	if (bTopView)
	{
		UpdateTopView(deltaSeconds);
		return;
	}

	UpdateOrbit(deltaSeconds);
}

bool ADeathCamActor::InitializeDeathCam(
	const FVector& inDeathLocation,
	AActor* inOtherActor,
	APlayerController* inPlayerController,
	UDeathCamDataAsset* inDeathCamData,
	AActor* inDeadActorToIgnore)
{
	if (!IsValid(inPlayerController) || !GetWorld())
	{
		UE_LOG(LogDeathCamActor, Warning, TEXT("InitializeDeathCam failed: PlayerController or World is invalid."));
		return false;
	}

	ownerPlayerController = inPlayerController;
	otherActor = inOtherActor;
	deadActorToIgnore = inDeadActorToIgnore;
	deathLocation = inDeathLocation;

	CacheSettings(inDeathCamData);

	if (IsValid(otherActor))
	{
		// 기본 DeathCam:
		// Death Location을 화면 중앙에 두고 Other 방향의 옆(90도)에서 바라보는 Side View입니다.
		bTopView = false;
		const FRotator initialRotation = CalculateOrbitTargetRotation();

		// orbitDistance는 이제 고정 거리가 아니라 최소 거리입니다.
		// Other가 멀면 Self를 중앙에 유지한 채 둘을 한 화면에 담을 수 있을 만큼 자동으로 뒤로 빠집니다.
		currentTargetDistance = CalculateRequiredOrbitDistance(initialRotation);
		ApplyCameraTransform(initialRotation, currentTargetDistance);
	}
	else
	{
		// 자살/환경 데미지처럼 Other가 없으면 처음부터 Top View입니다.
		bTopView = true;
		currentTargetDistance = topViewDistance;
		topViewTargetRotation = FRotator(topViewPitch, 0.0f, 0.0f);

		const FVector topViewLocation = ResolveTopViewLocation(currentTargetDistance);
		SetActorLocationAndRotation(
			topViewLocation,
			topViewTargetRotation,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}

	bDeathCamActive = true;
	SetActorTickEnabled(true);
	return true;
}

void ADeathCamActor::StopDeathCam()
{
	bDeathCamActive = false;
	SetActorTickEnabled(false);
}

void ADeathCamActor::CacheSettings(UDeathCamDataAsset* inDeathCamData)
{
	if (!IsValid(inDeathCamData))
	{
		UE_LOG(LogDeathCamActor, Warning, TEXT("DeathCamDataAsset is not assigned. Using built-in default values."));
		return;
	}

	orbitDistance = inDeathCamData->orbitDistance;
	orbitPitch = inDeathCamData->orbitPitch;
	orbitYawOffset = inDeathCamData->orbitYawOffset;
	orbitRotationSpeed = inDeathCamData->orbitRotationSpeed;
	topViewDistance = inDeathCamData->topViewDistance;
	topViewPitch = inDeathCamData->topViewPitch;
	topViewYawOffset = inDeathCamData->topViewYawOffset;
	topViewMoveSpeed = inDeathCamData->topViewMoveSpeed;
	topViewRotationSpeed = inDeathCamData->topViewRotationSpeed;
	orbitAlignmentTolerance = inDeathCamData->orbitAlignmentTolerance;
	screenSafeRatio = inDeathCamData->screenSafeRatio;
}

FRotator ADeathCamActor::CalculateOrbitTargetRotation() const
{
	if (!IsValid(otherActor))
	{
		return GetActorRotation();
	}

	const FVector toOther = otherActor->GetActorLocation() - deathLocation;
	const float otherDirectionYaw = toOther.IsNearlyZero()
		? GetActorRotation().Yaw
		: toOther.Rotation().Yaw;

	// 핵심:
	// Other를 정면으로 바라보는 것이 아니라 DeathLocation -> Other 방향에서 90도 옆으로 빠집니다.
	// Camera 위치는 deathLocation - Forward * Distance이고 Camera는 Forward를 바라보므로,
	// Death Location은 항상 화면 중앙에 위치하고 Other는 화면의 좌/우에 함께 보이게 됩니다.
	const float sideViewYaw = otherDirectionYaw + sideViewBaseYawOffset + orbitYawOffset;

	return FRotator(
		orbitPitch,
		FRotator::NormalizeAxis(sideViewYaw),
		0.0f);
}

float ADeathCamActor::CalculateRequiredOrbitDistance(const FRotator& viewRotation) const
{
	// Other가 없으면 최소 Orbit 거리만 사용합니다.
	if (!IsValid(otherActor))
	{
		return orbitDistance;
	}

	const UCameraComponent* cameraComponent = GetCameraComponent();
	if (!IsValid(cameraComponent))
	{
		return orbitDistance;
	}

	float aspectRatio = cameraComponent->AspectRatio;

	// 실제 PIE/게임 Viewport 비율을 우선 사용합니다.
	if (IsValid(ownerPlayerController))
	{
		int32 viewportSizeX = 0;
		int32 viewportSizeY = 0;
		ownerPlayerController->GetViewportSize(viewportSizeX, viewportSizeY);

		if (viewportSizeX > 0 && viewportSizeY > 0)
		{
			aspectRatio = static_cast<float>(viewportSizeX) / static_cast<float>(viewportSizeY);
		}
	}

	aspectRatio = FMath::Max(aspectRatio, 0.01f);
	const float safeRatio = FMath::Clamp(screenSafeRatio, 0.10f, 0.98f);

	const float horizontalHalfFovRadians = FMath::DegreesToRadians(cameraComponent->FieldOfView * 0.5f);
	const float horizontalLimit = FMath::Max(FMath::Tan(horizontalHalfFovRadians) * safeRatio, 0.001f);
	const float verticalLimit = FMath::Max((horizontalLimit / aspectRatio), 0.001f);

	// 카메라는 항상 DeathLocation을 정면 중앙으로 바라봅니다.
	// 따라서 DeathLocation -> Other 오프셋을 현재 카메라 축으로 분해하면,
	// Other를 프레임 안에 넣기 위해 필요한 최소 후퇴 거리를 직접 계산할 수 있습니다.
	const FVector toOther = otherActor->GetActorLocation() - deathLocation;
	const FRotationMatrix rotationMatrix(viewRotation);
	const FVector forward = rotationMatrix.GetUnitAxis(EAxis::X);
	const FVector right = rotationMatrix.GetUnitAxis(EAxis::Y);
	const FVector up = rotationMatrix.GetUnitAxis(EAxis::Z);

	const float forwardOffset = FVector::DotProduct(toOther, forward);
	const float lateralOffset = FMath::Abs(FVector::DotProduct(toOther, right));
	const float verticalOffset = FMath::Abs(FVector::DotProduct(toOther, up));

	// Camera Space에서 Other의 X(전방 깊이)는 Camera가 뒤로 빠질수록 증가합니다.
	// |Y/X| <= HorizontalLimit, |Z/X| <= VerticalLimit를 만족하는 최소 X를 구합니다.
	const float requiredDepthForHorizontal = lateralOffset / horizontalLimit;
	const float requiredDepthForVertical = verticalOffset / verticalLimit;
	const float requiredCameraSpaceDepth = FMath::Max(requiredDepthForHorizontal, requiredDepthForVertical);

	// CameraSpaceDepth = CameraDistance + ForwardOffset 이므로 필요한 Distance를 역산합니다.
	const float requiredDistance = requiredCameraSpaceDepth - forwardOffset;

	// orbitDistance는 디자이너가 지정하는 최소 거리입니다.
	// Other가 멀다고 임의의 최대 거리로 잘라 TopView를 강제하지 않습니다.
	return FMath::Max(orbitDistance, requiredDistance);
}

void ADeathCamActor::UpdateOrbit(float deltaSeconds)
{
	if (!IsValid(otherActor))
	{
		SwitchToTopView(TEXT("OtherInvalid"));
		return;
	}

	if (!IsValid(ownerPlayerController))
	{
		StopDeathCam();
		return;
	}

	const FRotator targetRotation = CalculateOrbitTargetRotation();
	const FRotator newRotation = FMath::RInterpConstantTo(
		GetActorRotation(),
		targetRotation,
		deltaSeconds,
		orbitRotationSpeed);

	// Other가 멀어져도 곧바로 TopView로 보내지 않습니다.
	// 먼저 DeathLocation을 중앙에 유지한 상태에서 둘을 한 화면에 담을 수 있도록 자동으로 후퇴합니다.
	currentTargetDistance = CalculateRequiredOrbitDistance(newRotation);
	ApplyCameraTransform(newRotation, currentTargetDistance);

	// Other가 벽 뒤에 가려져 실제로 볼 수 없다면 Side View를 유지할 의미가 없으므로 Top View로 전환합니다.
	if (!HasClearViewToOther())
	{
		SwitchToTopView(TEXT("VisibilityBlocked"));
		return;
	}

	// 현재 Side View에서 Other가 DeathCam 프레임 안에 들어오면 그대로 유지합니다.
	if (IsOtherInsideScreen())
	{
		return;
	}

	// Other 이동을 따라 Side View 목표 회전으로 가는 중에는 먼저 공전을 계속합니다.
	// 목표 Side View까지 도달했는데도 Other가 프레임 밖이면 그때만 Top View로 전환합니다.
	if (HasReachedOrbitRotation(targetRotation))
	{
		SwitchToTopView(TEXT("CannotFitInFrame"));
	}
}

void ADeathCamActor::UpdateTopView(float deltaSeconds)
{
	// Top View에 들어간 뒤에는 Other를 따라 공전하지 않습니다.
	// Death Location 바로 위의 고정된 목표 위치로 이동합니다.
	currentTargetDistance = FMath::FInterpConstantTo(
		currentTargetDistance,
		topViewDistance,
		deltaSeconds,
		topViewMoveSpeed);

	const FVector desiredTopViewLocation = ResolveTopViewLocation(currentTargetDistance);
	const FVector newLocation = FMath::VInterpConstantTo(
		GetActorLocation(),
		desiredTopViewLocation,
		deltaSeconds,
		topViewMoveSpeed);

	const FRotator newRotation = FMath::RInterpConstantTo(
		GetActorRotation(),
		topViewTargetRotation,
		deltaSeconds,
		topViewRotationSpeed);

	SetActorLocationAndRotation(
		newLocation,
		newRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

void ADeathCamActor::SwitchToTopView(const TCHAR* reason)
{
	if (bTopView)
	{
		return;
	}

	bTopView = true;

	UE_LOG(LogDeathCamActor, Log,
		TEXT("SwitchToTopView Reason=%s Other=%s CameraDistance=%.1f"),
		reason ? reason : TEXT("Unknown"),
		*GetNameSafe(otherActor),
		currentTargetDistance);

	// Top View는 전환 순간의 Yaw만 고정해 두고 이후 Other 움직임을 따라 회전하지 않습니다.
	topViewTargetRotation = FRotator(
		topViewPitch,
		FRotator::NormalizeAxis(GetActorRotation().Yaw + topViewYawOffset),
		0.0f);
}

FVector ADeathCamActor::ResolveCameraLocation(const FRotator& viewRotation, float targetDistance) const
{
	const FVector desiredCameraLocation = deathLocation - (viewRotation.Vector() * targetDistance);

	if (!GetWorld() || targetDistance <= KINDA_SMALL_NUMBER)
	{
		return desiredCameraLocation;
	}

	FCollisionQueryParams queryParams(SCENE_QUERY_STAT(DeathCamCameraCollision), false, this);
	queryParams.AddIgnoredActor(this);

	if (IsValid(deadActorToIgnore))
	{
		queryParams.AddIgnoredActor(deadActorToIgnore);
	}

	if (IsValid(otherActor))
	{
		queryParams.AddIgnoredActor(otherActor);
	}

	FHitResult hitResult;
	const bool bHit = GetWorld()->SweepSingleByChannel(
		hitResult,
		deathLocation,
		desiredCameraLocation,
		FQuat::Identity,
		ECC_Camera,
		FCollisionShape::MakeSphere(cameraProbeRadius),
		queryParams);

	return bHit ? hitResult.Location : desiredCameraLocation;
}

FVector ADeathCamActor::ResolveTopViewLocation(float targetDistance) const
{
	const FVector desiredCameraLocation = deathLocation + (FVector::UpVector * targetDistance);

	if (!GetWorld() || targetDistance <= KINDA_SMALL_NUMBER)
	{
		return desiredCameraLocation;
	}

	FCollisionQueryParams queryParams(SCENE_QUERY_STAT(DeathCamTopViewCollision), false, this);
	queryParams.AddIgnoredActor(this);

	if (IsValid(deadActorToIgnore))
	{
		queryParams.AddIgnoredActor(deadActorToIgnore);
	}

	if (IsValid(otherActor))
	{
		queryParams.AddIgnoredActor(otherActor);
	}

	FHitResult hitResult;
	const bool bHit = GetWorld()->SweepSingleByChannel(
		hitResult,
		deathLocation,
		desiredCameraLocation,
		FQuat::Identity,
		ECC_Camera,
		FCollisionShape::MakeSphere(cameraProbeRadius),
		queryParams);

	return bHit ? hitResult.Location : desiredCameraLocation;
}

void ADeathCamActor::ApplyCameraTransform(const FRotator& viewRotation, float targetDistance)
{
	const FVector cameraLocation = ResolveCameraLocation(viewRotation, targetDistance);

	// cameraLocation은 Death Location에서 viewRotation의 정반대 방향에 있으므로,
	// viewRotation을 그대로 적용하면 Death Location이 정확히 카메라 정면 중앙에 놓입니다.
	SetActorLocationAndRotation(
		cameraLocation,
		viewRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

bool ADeathCamActor::HasClearViewToOther() const
{
	if (!IsValid(otherActor) || !GetWorld())
	{
		return false;
	}

	FCollisionQueryParams queryParams(SCENE_QUERY_STAT(DeathCamVisibility), false, this);
	queryParams.AddIgnoredActor(this);

	if (IsValid(deadActorToIgnore))
	{
		queryParams.AddIgnoredActor(deadActorToIgnore);
	}

	FHitResult hitResult;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		hitResult,
		GetActorLocation(),
		otherActor->GetActorLocation(),
		ECC_Visibility,
		queryParams);

	return !bHit || hitResult.GetActor() == otherActor;
}

bool ADeathCamActor::IsOtherInsideScreen() const
{
	if (!IsValid(otherActor))
	{
		return false;
	}

	const UCameraComponent* cameraComponent = GetCameraComponent();
	if (!IsValid(cameraComponent))
	{
		return false;
	}

	// PlayerController의 이전 프레임 ViewTarget/Projection에 의존하지 않고,
	// 현재 DeathCamActor Transform 자체를 기준으로 Other가 카메라 Frustum 안에 있는지 계산합니다.
	const FVector cameraSpaceLocation = GetActorTransform().InverseTransformPosition(otherActor->GetActorLocation());

	// Camera의 Forward(+X) 뒤쪽에 있으면 화면 안에 들어올 수 없습니다.
	if (cameraSpaceLocation.X <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	float aspectRatio = cameraComponent->AspectRatio;

	// 실제 PIE/게임 Viewport 비율을 얻을 수 있으면 그것을 우선합니다.
	if (IsValid(ownerPlayerController))
	{
		int32 viewportSizeX = 0;
		int32 viewportSizeY = 0;
		ownerPlayerController->GetViewportSize(viewportSizeX, viewportSizeY);

		if (viewportSizeX > 0 && viewportSizeY > 0)
		{
			aspectRatio = static_cast<float>(viewportSizeX) / static_cast<float>(viewportSizeY);
		}
	}

	aspectRatio = FMath::Max(aspectRatio, 0.01f);

	const float horizontalHalfFovRadians = FMath::DegreesToRadians(cameraComponent->FieldOfView * 0.5f);
	const float horizontalLimit = FMath::Tan(horizontalHalfFovRadians);
	const float verticalLimit = horizontalLimit / aspectRatio;

	const float horizontalRatio = FMath::Abs(cameraSpaceLocation.Y / cameraSpaceLocation.X);
	const float verticalRatio = FMath::Abs(cameraSpaceLocation.Z / cameraSpaceLocation.X);

	return horizontalRatio <= horizontalLimit
		&& verticalRatio <= verticalLimit;
}

bool ADeathCamActor::HasReachedOrbitRotation(const FRotator& targetRotation) const
{
	const FRotator deltaRotation = (targetRotation - GetActorRotation()).GetNormalized();

	return FMath::Abs(deltaRotation.Pitch) <= orbitAlignmentTolerance
		&& FMath::Abs(deltaRotation.Yaw) <= orbitAlignmentTolerance;
}
