#include "OneWayTeleportActor.h"

#include "TeleportDataAsset.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace OneWayTeleportPrivate
{
	// 모든 Portal이 공유합니다. 따라서 A -> B로 이동하면서 B의 Overlap이 즉시
	// 발생해도 B가 같은 Character를 다시 텔레포트하지 않습니다.
	TMap<TWeakObjectPtr<AActor>, double> ReentryUnlockTimes;

	bool IsReentryLocked(AActor* actor, double currentTime)
	{
		const TWeakObjectPtr<AActor> weakActor(actor);
		if (const double* unlockTime = ReentryUnlockTimes.Find(weakActor))
		{
			return *unlockTime > currentTime;
		}

		return false;
	}

	void LockReentry(AActor* actor, double unlockTime)
	{
		ReentryUnlockTimes.Add(TWeakObjectPtr<AActor>(actor), unlockTime);
	}

	void UnlockReentry(AActor* actor)
	{
		ReentryUnlockTimes.Remove(TWeakObjectPtr<AActor>(actor));
	}

	void RemoveExpiredLocks(double currentTime)
	{
		for (auto iterator = ReentryUnlockTimes.CreateIterator(); iterator; ++iterator)
		{
			if (!iterator.Key().IsValid() || iterator.Value() <= currentTime)
			{
				iterator.RemoveCurrent();
			}
		}
	}
}

AOneWayTeleportActor::AOneWayTeleportActor()
{
	PrimaryActorTick.bCanEverTick = false;

	root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(root);

	// ---------------------------------------------------------------------
	// 실제 진입 판정 Collision
	// ---------------------------------------------------------------------

	entryCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("EntryCollision"));
	entryCollision->SetupAttachment(root);
	entryCollision->InitBoxExtent(FVector(100.0f, 100.0f, 100.0f));

	entryCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	entryCollision->SetCollisionObjectType(ECC_WorldDynamic);
	entryCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	entryCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	entryCollision->SetGenerateOverlapEvents(true);
	entryCollision->SetCanEverAffectNavigation(false);

	// ---------------------------------------------------------------------
	// 반투명 시각화 Cube
	// ---------------------------------------------------------------------

	portalVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PortalVisual"));
	portalVisual->SetupAttachment(root);

	portalVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	portalVisual->SetGenerateOverlapEvents(false);
	portalVisual->SetCanEverAffectNavigation(false);
	portalVisual->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> cubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));

	if (cubeMesh.Succeeded())
	{
		portalVisual->SetStaticMesh(cubeMesh.Object);
	}

#if WITH_EDITORONLY_DATA
	// ---------------------------------------------------------------------
	// 에디터 선택용 Handle
	//
	// 반투명 PortalVisual은 뷰포트에서 클릭하기 까다로울 수 있으므로
	// 포탈 위쪽에 불투명한 Sphere를 하나 표시합니다.
	// CreateEditorOnlyDefaultSubobject를 사용하므로 실제 게임/패키징에는 없습니다.
	// ---------------------------------------------------------------------

	editorSelectionHandle =
		CreateEditorOnlyDefaultSubobject<UStaticMeshComponent>(
			TEXT("EditorSelectionHandle"));

	if (editorSelectionHandle)
	{
		editorSelectionHandle->SetupAttachment(root);
		editorSelectionHandle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		editorSelectionHandle->SetGenerateOverlapEvents(false);
		editorSelectionHandle->SetCanEverAffectNavigation(false);
		editorSelectionHandle->SetCastShadow(false);
		editorSelectionHandle->SetHiddenInGame(true);

		static ConstructorHelpers::FObjectFinder<UStaticMesh> sphereMesh(
			TEXT("/Engine/BasicShapes/Sphere.Sphere"));

		if (sphereMesh.Succeeded())
		{
			editorSelectionHandle->SetStaticMesh(sphereMesh.Object);
		}

		// 기본 Sphere 지름이 100uu이므로 약 50uu 크기의 선택 Handle로 사용합니다.
		editorSelectionHandle->SetRelativeScale3D(FVector(0.2f));
	}
#endif
}

void AOneWayTeleportActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	UpdatePortalVisual();
}

void AOneWayTeleportActor::BeginPlay()
{
	Super::BeginPlay();

	entryCollision->OnComponentBeginOverlap.AddUniqueDynamic(
		this,
		&AOneWayTeleportActor::OnEntryBeginOverlap);

	if (IsValid(portalVisual))
	{
		portalVisual->SetHiddenInGame(!bShowPortalInGame);
	}
}

void AOneWayTeleportActor::UpdatePortalVisual()
{
	if (!IsValid(entryCollision) || !IsValid(portalVisual))
	{
		return;
	}

	// ---------------------------------------------------------------------
	// PortalVisual을 EntryCollision과 같은 위치/회전/크기로 맞춥니다.
	// Engine 기본 Cube는 100 x 100 x 100입니다.
	// ---------------------------------------------------------------------

	portalVisual->SetRelativeLocation(entryCollision->GetRelativeLocation());
	portalVisual->SetRelativeRotation(entryCollision->GetRelativeRotation());

	const FVector boxExtent = entryCollision->GetUnscaledBoxExtent();
	portalVisual->SetRelativeScale3D(boxExtent / 50.0f);

	// 에디터에서는 항상 보입니다.
	portalVisual->SetVisibility(true);

	// 체크 해제 시 실제 게임에서만 숨깁니다.
	portalVisual->SetHiddenInGame(!bShowPortalInGame);

#if WITH_EDITORONLY_DATA
	if (IsValid(editorSelectionHandle))
	{
		// 선택용 Handle을 EntryCollision 정중앙에 배치합니다.
		editorSelectionHandle->SetRelativeLocation(
			entryCollision->GetRelativeLocation());
		editorSelectionHandle->SetRelativeRotation(FRotator::ZeroRotator);
		editorSelectionHandle->SetVisibility(true);
	}
#endif

	// ---------------------------------------------------------------------
	// 색 / Alpha 갱신
	// ---------------------------------------------------------------------

	if (!IsValid(portalVisualMaterial))
	{
		return;
	}

	if (!IsValid(portalVisualMID)
		|| portalVisualMID->Parent != portalVisualMaterial)
	{
		portalVisualMID = UMaterialInstanceDynamic::Create(
			portalVisualMaterial,
			this);

		portalVisual->SetMaterial(0, portalVisualMID);
	}

	if (IsValid(portalVisualMID))
	{
		portalVisualMID->SetVectorParameterValue(
			TEXT("Color"),
			portalColor);

		portalVisualMID->SetScalarParameterValue(
			TEXT("Opacity"),
			portalOpacity);
	}
}

void AOneWayTeleportActor::OnEntryBeginOverlap(
	UPrimitiveComponent* overlappedComponent,
	AActor* otherActor,
	UPrimitiveComponent* otherComp,
	int32 otherBodyIndex,
	bool bFromSweep,
	const FHitResult& sweepResult)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!bTeleportEnabled)
	{
		return;
	}

	ACharacter* character = Cast<ACharacter>(otherActor);
	if (!IsValid(character))
	{
		return;
	}

	if (otherComp != character->GetCapsuleComponent())
	{
		return;
	}

	const double currentTime = GetWorld()->GetTimeSeconds();
	OneWayTeleportPrivate::RemoveExpiredLocks(currentTime);
	if (OneWayTeleportPrivate::IsReentryLocked(character, currentTime))
	{
		return;
	}

	if (!IsValid(exitTarget))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[OneWayTeleport] %s : exitTarget이 지정되지 않았습니다."),
			*GetName());

		return;
	}

	if (!IsValid(teleportDA))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[OneWayTeleport] %s : teleportDA가 지정되지 않았습니다."),
			*GetName());

		return;
	}

	TeleportCharacter(character);
}

void AOneWayTeleportActor::TeleportCharacter(ACharacter* character)
{
	if (!IsValid(character)
		|| !IsValid(exitTarget)
		|| !IsValid(teleportDA))
	{
		return;
	}

	const FVector exitLocation = exitTarget->GetActorLocation();
	const FRotator exitRotation = GetExitFacingRotation();
	const double currentTime = GetWorld()->GetTimeSeconds();
	OneWayTeleportPrivate::LockReentry(
		character,
		currentTime + FMath::Max(0.0f, reentryLockDuration));

	const bool bTeleported = character->SetActorLocationAndRotation(
		exitLocation,
		exitRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	if (!bTeleported)
	{
		OneWayTeleportPrivate::UnlockReentry(character);
		return;
	}

	if (AController* controller = character->GetController())
	{
		controller->SetControlRotation(exitRotation);

		if (APlayerController* playerController =
			Cast<APlayerController>(controller))
		{
			playerController->ClientSetRotation(exitRotation, false);
		}
	}

	character->LaunchCharacter(
		GetLaunchVelocity(),
		true,
		true);

	ApplyMoveLock(character, teleportDA->moveLockTime);
	character->ForceNetUpdate();
}

FRotator AOneWayTeleportActor::GetExitFacingRotation() const
{
	if (!IsValid(exitTarget))
	{
		return FRotator::ZeroRotator;
	}

	return FRotator(
		0.0f,
		exitTarget->GetActorRotation().Yaw,
		0.0f);
}

FVector AOneWayTeleportActor::GetLaunchVelocity() const
{
	if (!IsValid(exitTarget) || !IsValid(teleportDA))
	{
		return FVector::ZeroVector;
	}

	const FRotator launchRotation(
		static_cast<float>(teleportDA->launchAngle),
		exitTarget->GetActorRotation().Yaw,
		0.0f);

	return launchRotation.Vector() * teleportDA->launchPower;
}

void AOneWayTeleportActor::ApplyMoveLock_Implementation(
	ACharacter* character,
	float duration)
{
	if (!IsValid(character) || duration <= 0.0f)
	{
		return;
	}

	AController* controller = character->GetController();
	if (!IsValid(controller))
	{
		return;
	}

	controller->SetIgnoreMoveInput(true);
	character->StopJumping();

	TWeakObjectPtr<AController> weakController = controller;

	FTimerDelegate unlockDelegate;
	unlockDelegate.BindLambda(
		[weakController]()
		{
			if (AController* validController = weakController.Get())
			{
				validController->SetIgnoreMoveInput(false);
			}
		});

	FTimerHandle unlockHandle;
	GetWorldTimerManager().SetTimer(
		unlockHandle,
		unlockDelegate,
		duration,
		false);
}
