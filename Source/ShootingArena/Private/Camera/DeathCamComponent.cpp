#include "Camera/DeathCamComponent.h"

#include "Camera/DeathCamActor.h"
#include "Camera/DeathCamDataAsset.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeathCamComponent, Log, All);

UDeathCamComponent::UDeathCamComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// Client RPC를 owning PlayerController를 통해 전달하기 위해 Component Replication을 사용합니다.
	SetIsReplicatedByDefault(true);

	deathCamClass = ADeathCamActor::StaticClass();
}

void UDeathCamComponent::BeginPlay()
{
	Super::BeginPlay();

	ownerPlayerController = Cast<APlayerController>(GetOwner());

	if (!IsValid(ownerPlayerController))
	{
		UE_LOG(LogDeathCamComponent, Error, TEXT("DeathCamComponent must be owned by a PlayerController."));
	}
}

void UDeathCamComponent::EndPlay(const EEndPlayReason::Type endPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(stopRetryTimerHandle);
		GetWorld()->GetTimerManager().ClearTimer(destroyAfterBlendTimerHandle);
	}

	DestroyDeathCamImmediately();

	Super::EndPlay(endPlayReason);
}

void UDeathCamComponent::TickComponent(
	float deltaTime,
	ELevelTick tickType,
	FActorComponentTickFunction* thisTickFunction)
{
	Super::TickComponent(deltaTime, tickType, thisTickFunction);

	if (!bDeathCamActive || bStopRequested || !IsValid(deathCamActor) || !GetWorld())
	{
		return;
	}

	APlayerController* playerController = GetOwningPlayerController();
	if (!IsValid(playerController) || !playerController->IsLocalController())
	{
		return;
	}

	if (GetWorld()->GetTimeSeconds() < enforceViewTargetAfterTime)
	{
		return;
	}

	// DeathCam 활성 중에는 DeathCam이 카메라 소유자입니다.
	// 기존 카메라 로직이 ViewTarget을 다시 Pawn으로 돌리면 즉시 DeathCam으로 복구합니다.
	if (playerController->GetViewTarget() != deathCamActor)
	{
		UE_LOG(LogDeathCamComponent, Warning,
			TEXT("DeathCam ViewTarget was overridden. Restoring DeathCamActor."));

		playerController->SetViewTarget(deathCamActor);
	}
}

void UDeathCamComponent::StartDeathCam(APawn* deadPawn, AController* instigatedBy, AActor* damageCauser)
{
	APlayerController* playerController = GetOwningPlayerController();
	if (!IsValid(playerController) || !IsValid(deadPawn))
	{
		UE_LOG(LogDeathCamComponent, Warning,
			TEXT("StartDeathCam ignored: PlayerController or DeadPawn is invalid."));
		return;
	}

	const FVector deathLocation = deadPawn->GetActorLocation();
	AActor* otherActor = ResolveOtherActor(deadPawn, instigatedBy, damageCauser);

	// Owning Client에서도 OnDeath가 발생하는 프로젝트 구조라면 서버 RPC를 기다리지 않고 즉시 시작합니다.
	// 이 경로가 기획에서 요구한 '사망 직후 텀 없는 DeathCam'을 담당합니다.
	if (playerController->IsLocalController())
	{
		StartDeathCamLocal(deathLocation, otherActor, deadPawn);
	}

	// Remote Client용 서버 Controller에서는 owning client로 RPC를 보냅니다.
	// 로컬 Listen Server Controller에는 위에서 이미 시작했으므로 중복 RPC를 보내지 않습니다.
	if (playerController->HasAuthority() && !playerController->IsLocalController())
	{
		ClientStartDeathCam(deathLocation, otherActor, deadPawn);
	}
}

void UDeathCamComponent::StopDeathCam(APawn* newPawn)
{
	APlayerController* playerController = GetOwningPlayerController();
	if (!IsValid(playerController))
	{
		return;
	}

	// Stop 요청은 기존 구조대로 서버를 기준으로 owning client에 전달합니다.
	if (!playerController->HasAuthority())
	{
		return;
	}

	if (playerController->IsLocalController())
	{
		RequestStopDeathCamLocal(newPawn);
		return;
	}

	ClientStopDeathCam(newPawn);
}

void UDeathCamComponent::ClientStartDeathCam_Implementation(
	FVector deathLocation,
	AActor* otherActor,
	AActor* deadActorToIgnore)
{
	StartDeathCamLocal(deathLocation, otherActor, deadActorToIgnore);
}

void UDeathCamComponent::ClientStopDeathCam_Implementation(APawn* newPawn)
{
	RequestStopDeathCamLocal(newPawn);
}

APlayerController* UDeathCamComponent::GetOwningPlayerController()
{
	if (!IsValid(ownerPlayerController))
	{
		ownerPlayerController = Cast<APlayerController>(GetOwner());
	}

	return ownerPlayerController;
}

AActor* UDeathCamComponent::ResolveOtherActor(
	APawn* deadPawn,
	AController* instigatedBy,
	AActor* damageCauser) const
{
	// 최종 기획에서 OtherActor는 사실상 Killer입니다.
	// PlayerController BP를 지금 수정하지 않아도 테스트할 수 있도록
	// 가능한 경우 C++ 안에서 Killer Pawn까지 복구합니다.

	// 1순위: OnDeath의 InstigatedBy가 제어하는 Pawn.
	// 나중에 PlayerController에서 InstigatedBy 핀을 연결하면 이 경로가 가장 정확합니다.
	if (IsValid(instigatedBy))
	{
		APawn* instigatorPawn = instigatedBy->GetPawn();
		if (IsValid(instigatorPawn) && instigatorPawn != deadPawn)
		{
			return instigatorPawn;
		}
	}

	if (!IsValid(damageCauser) || damageCauser == deadPawn)
	{
		return nullptr;
	}

	// 2순위: Projectile/Weapon 등이 DamageCauser인 경우 Actor의 Instigator Pawn을 우선 사용합니다.
	// 현재 PlayerController에서 InstigatedBy 핀이 비어 있어도 일반적인 Projectile 구조라면
	// 이 경로를 통해 실제 Killer Pawn을 찾을 수 있습니다.
	APawn* damageInstigatorPawn = damageCauser->GetInstigator();
	if (IsValid(damageInstigatorPawn) && damageInstigatorPawn != deadPawn)
	{
		return damageInstigatorPawn;
	}

	// 3순위: DamageCauser Owner가 Pawn 또는 Controller인 경우도 확인합니다.
	AActor* damageOwner = damageCauser->GetOwner();
	if (APawn* ownerPawn = Cast<APawn>(damageOwner))
	{
		if (ownerPawn != deadPawn)
		{
			return ownerPawn;
		}
	}

	if (AController* ownerController = Cast<AController>(damageOwner))
	{
		APawn* ownerControllerPawn = ownerController->GetPawn();
		if (IsValid(ownerControllerPawn) && ownerControllerPawn != deadPawn)
		{
			return ownerControllerPawn;
		}
	}

	// 마지막 fallback: Killer Pawn까지 복구할 수 없는 경우 기존처럼 DamageCauser 자체를 사용합니다.
	return damageCauser;
}

void UDeathCamComponent::StartDeathCamLocal(
	const FVector& deathLocation,
	AActor* otherActor,
	AActor* deadActorToIgnore)
{
	APlayerController* playerController = GetOwningPlayerController();
	if (!IsValid(playerController) || !playerController->IsLocalController() || !GetWorld())
	{
		return;
	}

	// 같은 사망이 Local OnDeath와 서버 Client RPC 양쪽에서 들어오는 경우 두 번째 실행은 무시합니다.
	// Actor 포인터가 RPC 시점에 달라졌더라도 고정 Death Location이 같으면 같은 사망으로 취급합니다.
	const bool bSameDeathAlreadyActive = bDeathCamActive
		&& IsValid(deathCamActor)
		&& (
			activeDeadActor.Get() == deadActorToIgnore
			|| deathCamActor->GetDeathLocation().Equals(deathLocation, 1.0f));

	if (bSameDeathAlreadyActive)
	{
		return;
	}

	// 이전 Stop/Destroy 예약이 있으면 모두 취소합니다.
	GetWorld()->GetTimerManager().ClearTimer(stopRetryTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(destroyAfterBlendTimerHandle);

	bStopRequested = false;
	pendingNewPawn = nullptr;

	// 이전 DeathCam Actor가 비정상적으로 남아 있어도 새 사망을 막지 않습니다.
	DestroyDeathCamImmediately();

	TSubclassOf<ADeathCamActor> spawnClass = deathCamClass;
	if (!spawnClass)
	{
		spawnClass = ADeathCamActor::StaticClass();
	}

	FActorSpawnParameters spawnParameters;
	spawnParameters.Owner = playerController;
	spawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	deathCamActor = GetWorld()->SpawnActor<ADeathCamActor>(
		spawnClass,
		deathLocation,
		FRotator::ZeroRotator,
		spawnParameters);

	if (!IsValid(deathCamActor))
	{
		UE_LOG(LogDeathCamComponent, Error, TEXT("Failed to spawn DeathCamActor."));
		return;
	}

	if (!deathCamActor->InitializeDeathCam(
		deathLocation,
		otherActor,
		playerController,
		deathCamData,
		deadActorToIgnore))
	{
		UE_LOG(LogDeathCamComponent, Error, TEXT("DeathCamActor InitializeDeathCam failed."));
		DestroyDeathCamImmediately();
		return;
	}

	activeDeadActor = deadActorToIgnore;
	bDeathCamActive = true;
	SetComponentTickEnabled(true);

	// 기획 요구사항:
	// 사망 -> DeathCam 진입에는 Blend/Delay를 두지 않습니다.
	// 사망 이벤트를 받은 프레임에 즉시 DeathCamActor를 ViewTarget으로 지정합니다.
	playerController->SetViewTarget(deathCamActor);

	// 시작 즉시부터 ViewTarget 소유권을 감시합니다.
	enforceViewTargetAfterTime = GetWorld()->GetTimeSeconds();

	UE_LOG(LogDeathCamComponent, Log,
		TEXT("DeathCam started immediately. Actor=%s DeathLocation=%s Other=%s"),
		*GetNameSafe(deathCamActor),
		*deathLocation.ToString(),
		*GetNameSafe(otherActor));
}

void UDeathCamComponent::RequestStopDeathCamLocal(APawn* newPawn)
{
	APlayerController* playerController = GetOwningPlayerController();
	if (!IsValid(playerController) || !playerController->IsLocalController() || !GetWorld())
	{
		return;
	}

	if (!bDeathCamActive && !IsValid(deathCamActor))
	{
		return;
	}

	bStopRequested = true;
	pendingNewPawn = newPawn;

	GetWorld()->GetTimerManager().ClearTimer(stopRetryTimerHandle);
	TryCompleteStopDeathCam();
}

void UDeathCamComponent::TryCompleteStopDeathCam()
{
	APlayerController* playerController = GetOwningPlayerController();
	if (!IsValid(playerController) || !playerController->IsLocalController() || !GetWorld())
	{
		return;
	}

	APawn* viewTargetPawn = pendingNewPawn.Get();

	if (!IsValid(viewTargetPawn))
	{
		viewTargetPawn = playerController->GetPawn();
	}

	const bool bStillOldDeadPawn = IsValid(activeDeadActor.Get())
		&& viewTargetPawn == activeDeadActor.Get()
		&& !IsValid(pendingNewPawn.Get());

	if (!IsValid(viewTargetPawn) || bStillOldDeadPawn)
	{
		GetWorld()->GetTimerManager().SetTimer(
			stopRetryTimerHandle,
			this,
			&UDeathCamComponent::TryCompleteStopDeathCam,
			0.05f,
			false);
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(stopRetryTimerHandle);

	if (IsValid(deathCamActor))
	{
		deathCamActor->StopDeathCam();
	}

	bDeathCamActive = false;
	SetComponentTickEnabled(false);

	// 이 값은 이제 '부활 후 새 Pawn으로 돌아갈 때'만 사용합니다.
	const float blendTime = GetViewTargetBlendTime();
	playerController->SetViewTargetWithBlend(
		viewTargetPawn,
		blendTime,
		EViewTargetBlendFunction::VTBlend_Cubic,
		0.0f,
		false);

	if (blendTime <= KINDA_SMALL_NUMBER)
	{
		FinishStopDeathCam();
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		destroyAfterBlendTimerHandle,
		this,
		&UDeathCamComponent::FinishStopDeathCam,
		blendTime,
		false);
}

void UDeathCamComponent::FinishStopDeathCam()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(stopRetryTimerHandle);
		GetWorld()->GetTimerManager().ClearTimer(destroyAfterBlendTimerHandle);
	}

	DestroyDeathCamImmediately();

	pendingNewPawn = nullptr;
	activeDeadActor = nullptr;
	bDeathCamActive = false;
	bStopRequested = false;
	enforceViewTargetAfterTime = 0.0;
	SetComponentTickEnabled(false);

	UE_LOG(LogDeathCamComponent, Log, TEXT("DeathCam stopped and actor destroyed."));
}

void UDeathCamComponent::DestroyDeathCamImmediately()
{
	if (IsValid(deathCamActor))
	{
		deathCamActor->StopDeathCam();
		deathCamActor->Destroy();
	}

	deathCamActor = nullptr;
	bDeathCamActive = false;
}

float UDeathCamComponent::GetViewTargetBlendTime() const
{
	return IsValid(deathCamData) ? deathCamData->viewTargetBlendTime : 0.2f;
}
