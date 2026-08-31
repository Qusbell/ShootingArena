#include "Camera/DeathCamActor.h"

#include "Camera/DeathCamDataAsset.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"

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

	UpdateDeathCam(deltaSeconds);
}

void ADeathCamActor::EndPlay(const EEndPlayReason::Type endPlayReason)
{
	// StopDeathCam을 거치지 않고 World 종료/강제 Destroy가 발생해도
	// Killer의 CustomDepth 상태가 남지 않도록 항상 원복합니다.
	RemoveKillerHighlight();

	Super::EndPlay(endPlayReason);
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

	// 최종 기획: Center = 사망 위치 + Offset.
	// 기획서의 Offset 자료형이 Float이므로 현재 구현에서는 World Z 방향 Offset으로 해석합니다.
	centerLocation = deathLocation + (FVector::UpVector * centerOffset);

	const FVector killerDirection = CalculateKillerDirection();

	// 최종 기획의 "초기 DeathCam 위치"를 먼저 만들고,
	// Center와 그 위치 사이의 거리를 MaxDistance로 확정합니다.
	const FVector initialDesiredLocation = centerLocation - (killerDirection * initialCameraDistance);
	maxDistance = FVector::Distance(centerLocation, initialDesiredLocation);

	// 초기 위치가 벽 안쪽이면 Center -> 초기 위치 Sweep으로 가능한 지점까지만 이동합니다.
	// 실제 Camera-Center 거리가 MaxDistance보다 짧아지는 것은 기획상 허용됩니다.
	const FVector initialCameraLocation = ResolveInitialCameraLocation(initialDesiredLocation);
	const FVector clampedInitialLocation = ClampCameraLocation(initialCameraLocation, killerDirection);
	const FRotator initialRotation = CalculateLookAtCenterRotation(clampedInitialLocation);

	SetActorLocationAndRotation(
		clampedInitialLocation,
		initialRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	// Post Process Material은 이 로컬 DeathCam 카메라에만 적용합니다.
	// 따라서 다른 플레이어의 일반 카메라에는 Killer Highlight가 보이지 않습니다.
	ApplyKillerHighlightPostProcess();
	ApplyKillerHighlight();

	bDeathCamActive = true;
	SetActorTickEnabled(true);

	UE_LOG(LogDeathCamActor, Log,
		TEXT("DeathCam initialized. Center=%s Killer=%s MaxDistance=%.1f"),
		*centerLocation.ToString(),
		*GetNameSafe(otherActor),
		maxDistance);

	return true;
}

void ADeathCamActor::StopDeathCam()
{
	if (!bDeathCamActive && killerHighlightStates.IsEmpty())
	{
		return;
	}

	bDeathCamActive = false;
	SetActorTickEnabled(false);
	RemoveKillerHighlight();
}

void ADeathCamActor::CacheSettings(UDeathCamDataAsset* inDeathCamData)
{
	if (!IsValid(inDeathCamData))
	{
		UE_LOG(LogDeathCamActor, Warning, TEXT("DeathCamDataAsset is not assigned. Using built-in default values."));
		return;
	}

	centerOffset = inDeathCamData->centerOffset;
	cameraMoveInterpSpeed = inDeathCamData->cameraMoveInterpSpeed;
	bEnableKillerHighlight = inDeathCamData->bEnableKillerHighlight;
	killerHighlightMaterial = inDeathCamData->GetKillerHighlightMaterial();
	killerHighlightStencilValue = inDeathCamData->killerHighlightStencilValue;

	// Legacy OrbitDistance는 최종 기획에서 제거했습니다.
	// 기존 DataAsset에 initialCameraDistance가 0으로 저장되어 있어도 카메라가 Center에 붙지 않도록
	// 안전 기본값 600을 사용합니다. DataAsset을 한 번 열어 원하는 값을 명시적으로 저장하는 것을 권장합니다.
	initialCameraDistance = inDeathCamData->initialCameraDistance > KINDA_SMALL_NUMBER
		? inDeathCamData->initialCameraDistance
		: 600.0f;

	initialCameraDistance = FMath::Max(initialCameraDistance, 0.0f);
	cameraMoveInterpSpeed = FMath::Max(cameraMoveInterpSpeed, 0.0f);
	killerHighlightStencilValue = FMath::Clamp(killerHighlightStencilValue, 0, 255);
}

void ADeathCamActor::UpdateDeathCam(float deltaSeconds)
{
	if (!IsValid(ownerPlayerController))
	{
		StopDeathCam();
		return;
	}

	// 최종 기획에는 Killer가 없는 사망의 별도 연출이 정의되어 있지 않습니다.
	// 이 경우 TopView 같은 임의 연출로 전환하지 않고 현재 위치에서 Center만 계속 바라봅니다.
	if (!IsValid(otherActor))
	{
		SetActorRotation(CalculateLookAtCenterRotation(GetActorLocation()));
		return;
	}

	const FVector killerDirection = CalculateKillerDirection();
	const FVector desiredCameraLocation = CalculateBaseCameraLocation(killerDirection);

	// 기획의 "즉시 이동하지 않고 부드럽게(Lerp) 이동"을 보간으로 처리합니다.
	const FVector interpolatedLocation = FMath::VInterpTo(
		GetActorLocation(),
		desiredCameraLocation,
		deltaSeconds,
		cameraMoveInterpSpeed);

	const FVector cameraMove = interpolatedLocation - GetActorLocation();
	const FVector newCameraLocation = MoveCameraWithCollision(cameraMove, killerDirection);
	const FRotator newCameraRotation = CalculateLookAtCenterRotation(newCameraLocation);

	SetActorLocationAndRotation(
		newCameraLocation,
		newCameraRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

FVector ADeathCamActor::CalculateKillerDirection() const
{
	if (IsValid(otherActor))
	{
		const FVector toKiller = otherActor->GetActorLocation() - centerLocation;
		if (!toKiller.IsNearlyZero())
		{
			return toKiller.GetSafeNormal();
		}
	}

	// Killer가 없는 사망의 연출은 기획서에서 정의되지 않았습니다.
	// 다만 0 Vector로 인한 NaN/잘못된 Transform을 막기 위한 기술적 fallback만 둡니다.
	if (IsValid(deadActorToIgnore))
	{
		const FVector fallbackDirection = deadActorToIgnore->GetActorForwardVector();
		if (!fallbackDirection.IsNearlyZero())
		{
			return fallbackDirection.GetSafeNormal();
		}
	}

	return FVector::ForwardVector;
}

FVector ADeathCamActor::CalculateBaseCameraLocation(const FVector& killerDirection) const
{
	// 최종 기획의 기본 위치 관계:
	// Camera - Center - Killer가 같은 선상이며 Camera는 Killer 반대 방향입니다.
	return centerLocation - (killerDirection * maxDistance);
}

FVector ADeathCamActor::ResolveInitialCameraLocation(const FVector& desiredCameraLocation) const
{
	if (!GetWorld() || maxDistance <= KINDA_SMALL_NUMBER)
	{
		return desiredCameraLocation;
	}

	FCollisionQueryParams queryParams(SCENE_QUERY_STAT(DeathCamInitialCollision), false, this);
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
		centerLocation,
		desiredCameraLocation,
		FQuat::Identity,
		ECC_Camera,
		FCollisionShape::MakeSphere(cameraProbeRadius),
		queryParams);

	if (!bHit)
	{
		return desiredCameraLocation;
	}

	// Sphere 중심을 충돌면에 정확히 붙여두면 첫 Tick부터 같은 면을 Time=0으로
	// 재충돌할 수 있으므로 표면 법선 방향으로 아주 조금 떼어 둡니다.
	const FVector hitNormal = hitResult.ImpactNormal.GetSafeNormal();
	FVector safeLocation = hitResult.Location;

	if (!hitNormal.IsNearlyZero())
	{
		const float pushOutDistance = hitResult.bStartPenetrating
			? hitResult.PenetrationDepth + cameraCollisionSkin
			: cameraCollisionSkin;

		safeLocation += hitNormal * pushOutDistance;
	}

	return safeLocation;
}

FVector ADeathCamActor::MoveCameraWithCollision(
	const FVector& cameraMove,
	const FVector& killerDirection) const
{
	if (cameraMove.IsNearlyZero() || !GetWorld())
	{
		return ClampCameraLocation(GetActorLocation(), killerDirection);
	}

	FCollisionQueryParams queryParams(SCENE_QUERY_STAT(DeathCamSlideCollision), false, this);
	queryParams.AddIgnoredActor(this);

	if (IsValid(deadActorToIgnore))
	{
		queryParams.AddIgnoredActor(deadActorToIgnore);
	}

	if (IsValid(otherActor))
	{
		queryParams.AddIgnoredActor(otherActor);
	}

	FVector resolvedLocation = GetActorLocation();
	FVector remainingMove = cameraMove;

	// 한 번만 Slide하면 모서리에서 두 번째 면에 걸린 순간 정지하기 쉽습니다.
	// 제한된 횟수만 반복하면서 매 충돌마다 남은 이동량의 벽 안쪽 성분을 제거합니다.
	for (int32 iteration = 0; iteration < maxSlideIterations; ++iteration)
	{
		if (remainingMove.IsNearlyZero(0.01f))
		{
			break;
		}

		const FVector desiredLocation = resolvedLocation + remainingMove;

		FHitResult hitResult;
		const bool bHit = GetWorld()->SweepSingleByChannel(
			hitResult,
			resolvedLocation,
			desiredLocation,
			FQuat::Identity,
			ECC_Camera,
			FCollisionShape::MakeSphere(cameraProbeRadius),
			queryParams);

		if (!bHit)
		{
			resolvedLocation = desiredLocation;
			remainingMove = FVector::ZeroVector;
			break;
		}

		const FVector hitNormal = hitResult.ImpactNormal.GetSafeNormal();

		// 먼저 실제 충돌 지점까지 이동한 뒤, 다음 Sweep이 같은 면에서 Time=0으로
		// 다시 막히지 않도록 표면에서 아주 조금 떨어뜨립니다.
		resolvedLocation = hitResult.Location;

		if (!hitNormal.IsNearlyZero())
		{
			const float pushOutDistance = hitResult.bStartPenetrating
				? hitResult.PenetrationDepth + cameraCollisionSkin
				: cameraCollisionSkin;

			resolvedLocation += hitNormal * pushOutDistance;
		}

		// 충돌하기 전 원래 이동량 중 실제로 남은 비율만 계산합니다.
		// hitResult.Location에서 desiredLocation을 다시 빼는 방식은 위 Skin 보정량까지
		// 이동량에 섞일 수 있으므로 Hit.Time을 기준으로 남은 양을 구합니다.
		const float remainingFraction = 1.0f - FMath::Clamp(hitResult.Time, 0.0f, 1.0f);
		const FVector moveAfterImpact = remainingMove * remainingFraction;

		// 기획 공식:
		// Adjusted_Move = Camera_Move - Dot(Camera_Move, HitNormal) * HitNormal
		// FVector::VectorPlaneProject는 위 식과 동일하게 Normal 방향 성분을 제거합니다.
		remainingMove = hitNormal.IsNearlyZero()
			? FVector::ZeroVector
			: FVector::VectorPlaneProject(moveAfterImpact, hitNormal);
	}

	return ClampCameraLocation(resolvedLocation, killerDirection);
}

FVector ADeathCamActor::ClampCameraLocation(
	const FVector& candidateLocation,
	const FVector& killerDirection) const
{
	FVector relativeLocation = candidateLocation - centerLocation;

	// 1) Camera는 Center 기준 MaxDistance보다 멀어질 수 없습니다.
	const float distanceFromCenter = relativeLocation.Size();
	if (distanceFromCenter > maxDistance && distanceFromCenter > KINDA_SMALL_NUMBER)
	{
		relativeLocation = relativeLocation.GetSafeNormal() * maxDistance;
	}

	// 2) Camera는 Center를 넘어 Killer가 있는 방향으로 이동할 수 없습니다.
	// Center를 통과하는 평면을 기준으로 Killer 쪽 성분이 생기면 해당 성분만 제거합니다.
	const float killerSideAmount = FVector::DotProduct(relativeLocation, killerDirection);
	if (killerSideAmount > 0.0f)
	{
		relativeLocation -= killerDirection * killerSideAmount;
	}

	return centerLocation + relativeLocation;
}

FRotator ADeathCamActor::CalculateLookAtCenterRotation(const FVector& cameraLocation) const
{
	const FVector toCenter = centerLocation - cameraLocation;

	if (toCenter.IsNearlyZero())
	{
		return GetActorRotation();
	}

	return toCenter.Rotation();
}

void ADeathCamActor::ApplyKillerHighlightPostProcess()
{
	if (!bEnableKillerHighlight || !IsValid(killerHighlightMaterial))
	{
		if (bEnableKillerHighlight && !IsValid(killerHighlightMaterial))
		{
			UE_LOG(LogDeathCamActor, Warning,
				TEXT("Killer Highlight is enabled, but the Material for the selected Highlight Type is not assigned in DeathCamDataAsset."));
		}
		return;
	}

	UCameraComponent* cameraComponent = GetCameraComponent();
	if (!IsValid(cameraComponent))
	{
		UE_LOG(LogDeathCamActor, Warning, TEXT("Killer Highlight Post Process was not applied: CameraComponent is invalid."));
		return;
	}

	// ACameraActor가 소유한 이 카메라에만 Blendable을 추가합니다.
	// 레벨의 PostProcessVolume이나 PlayerController 수정은 필요하지 않습니다.
	cameraComponent->PostProcessSettings.AddBlendable(killerHighlightMaterial, 1.0f);
	cameraComponent->PostProcessBlendWeight = 1.0f;
}

void ADeathCamActor::ApplyKillerHighlight()
{
	// DeathCam이 다시 시작되는 경우를 대비해,
	// 이전 Highlight 대상의 CustomDepth / Stencil 상태를 먼저 원래 값으로 복구합니다.
	RemoveKillerHighlight();

	// Highlight 기능이 비활성화되어 있거나 Killer가 유효하지 않으면 처리하지 않습니다.
	if (!bEnableKillerHighlight || !IsValid(otherActor))
	{
		return;
	}

	/**
	 * 지정한 Actor의 모든 PrimitiveComponent에
	 * DeathCam Highlight용 CustomDepth / Stencil 값을 적용합니다.
	 *
	 * 적용 전에 각 Component의 기존 CustomDepth / Stencil 상태를 저장해 두며,
	 * DeathCam 종료 시 RemoveKillerHighlight()에서 원래 상태로 복구합니다.
	 *
	 * Killer 본체와 장착 무기에는 동일한 Stencil 값을 적용합니다.
	 * 이를 통해 장착 무기가 Killer의 몸을 가리고 있어도
	 * 무기를 별도의 장애물로 잘못 판단하지 않도록 합니다.
	 */
	auto ApplyStencilToActor =
		[this](AActor* targetActor, const int32 stencilValue)
		{
			if (!IsValid(targetActor))
			{
				return;
			}

			// Actor에 포함된 모든 PrimitiveComponent를 가져옵니다.
			TInlineComponentArray<UPrimitiveComponent*> primitiveComponents;
			targetActor->GetComponents(primitiveComponents);

			// 저장 배열의 재할당을 줄이기 위해 필요한 크기를 미리 확보합니다.
			killerHighlightStates.Reserve(
				killerHighlightStates.Num() + primitiveComponents.Num());

			for (UPrimitiveComponent* primitiveComponent : primitiveComponents)
			{
				if (!IsValid(primitiveComponent))
				{
					continue;
				}

				FHighlightComponentState& state =
					killerHighlightStates.AddDefaulted_GetRef();

				// DeathCam 종료 후 원래 상태로 되돌릴 수 있도록
				// 현재 CustomDepth / Stencil 설정을 저장합니다.
				state.component = primitiveComponent;
				state.bRenderCustomDepth = primitiveComponent->bRenderCustomDepth;
				state.customDepthStencilValue = primitiveComponent->CustomDepthStencilValue;

				// DeathCam Post Process Material에서 Killer 그룹으로 인식할 수 있도록
				// CustomDepth와 지정된 Stencil 값을 적용합니다.
				primitiveComponent->SetRenderCustomDepth(true);
				primitiveComponent->SetCustomDepthStencilValue(stencilValue);
			}
		};

	// Killer 본체에 DeathCam Highlight용 Stencil 값을 적용합니다.
	ApplyStencilToActor(otherActor, killerHighlightStencilValue);

	/**
	 * 무기는 Killer와 별도의 Actor로 존재하면서 Character에 Attach되어 있으므로,
	 * Killer의 Component만 조회해서는 Weapon Mesh를 처리할 수 없습니다.
	 *
	 * 따라서 Killer에게 Attach된 Actor를 별도로 탐색한 뒤,
	 * DeathCamWeapon 태그가 지정된 Actor에도 동일한 Stencil 값을 적용합니다.
	 */
	TArray<AActor*> attachedActors;

	otherActor->GetAttachedActors(
		attachedActors,
		true,   // 기존 배열 내용을 초기화한 뒤 결과를 저장합니다.
		true    // 하위에 Attach된 Actor까지 재귀적으로 탐색합니다.
	);

	for (AActor* attachedActor : attachedActors)
	{
		if (!IsValid(attachedActor))
		{
			continue;
		}

		// DeathCam Highlight 처리 대상인 장착 무기만 선택합니다.
		if (!attachedActor->ActorHasTag(TEXT("DeathCamWeapon")))
		{
			continue;
		}

		/**
		 * 장착 무기도 Killer 본체와 동일한 Stencil 그룹으로 처리합니다.
		 *
		 * 무기가 화면에 직접 보이는 경우에는 SceneDepth와 CustomDepth가 거의 동일하므로
		 * Occlusion 조건을 만족하지 않아 Highlight가 표시되지 않습니다.
		 *
		 * 반대로 Killer와 무기가 벽 뒤에 함께 가려진 경우에는
		 * 둘을 하나의 연속된 Killer 실루엣으로 판단할 수 있어,
		 * 무기와 몸이 겹치는 부분에서 Highlight가 끊기는 현상을 방지합니다.
		 */
		ApplyStencilToActor(attachedActor, killerHighlightStencilValue);
	}
}

void ADeathCamActor::RemoveKillerHighlight()
{
	for (const FHighlightComponentState& state : killerHighlightStates)
	{
		UPrimitiveComponent* primitiveComponent = state.component.Get();
		if (!IsValid(primitiveComponent))
		{
			continue;
		}

		primitiveComponent->SetRenderCustomDepth(state.bRenderCustomDepth);
		primitiveComponent->SetCustomDepthStencilValue(state.customDepthStencilValue);
	}

	killerHighlightStates.Reset();
}
