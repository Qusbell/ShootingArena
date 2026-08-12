#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DeathCamComponent.generated.h"

class AController;
class ADeathCamActor;
class APawn;
class APlayerController;
class UDeathCamDataAsset;

/**
 * 기존 PlayerController에 붙여 사용하는 DeathCam 전용 Component입니다.
 *
 * 네트워크 경계, DeathCam Actor 생명주기, ViewTarget 소유권만 담당합니다.
 * 기존 Controller / Life / Respawn 시스템의 로직은 이 Component가 수정하지 않습니다.
 */
UCLASS(ClassGroup = (Camera), meta = (BlueprintSpawnableComponent))
class SHOOTINGARENA_API UDeathCamComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeathCamComponent();

	virtual void TickComponent(
		float deltaTime,
		ELevelTick tickType,
		FActorComponentTickFunction* thisTickFunction) override;

	/** 기존 OnPawnDeath에서 그대로 호출합니다. */
	UFUNCTION(BlueprintCallable, Category = "DeathCam")
	void StartDeathCam(APawn* deadPawn, AController* instigatedBy, AActor* damageCauser);

	/** 기존 ReceivePossess에서 그대로 호출합니다. */
	UFUNCTION(BlueprintCallable, Category = "DeathCam")
	void StopDeathCam(APawn* newPawn);

	UFUNCTION(BlueprintPure, Category = "DeathCam")
	bool IsDeathCamActive() const { return bDeathCamActive; }

	UFUNCTION(BlueprintPure, Category = "DeathCam")
	ADeathCamActor* GetDeathCamActor() const { return deathCamActor; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:
	/** 비워두면 순수 C++ ADeathCamActor를 생성합니다. */
	UPROPERTY(EditAnywhere, Category = "DeathCam|Config")
	TSubclassOf<ADeathCamActor> deathCamClass;

	/** 기존 DA_DeathCamera_Default를 그대로 지정하면 됩니다. */
	UPROPERTY(EditAnywhere, Category = "DeathCam|Config")
	TObjectPtr<UDeathCamDataAsset> deathCamData;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> ownerPlayerController;

	UPROPERTY(Transient)
	TObjectPtr<ADeathCamActor> deathCamActor;

	/** 현재 DeathCam이 시작된 사망 Pawn. 같은 사망의 Client RPC 중복 실행을 판별할 때도 사용합니다. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> activeDeadActor;

	/** 서버가 넘긴 새 Pawn. RPC 시점에 아직 클라이언트에서 유효하지 않으면 Controller Pawn 갱신을 기다립니다. */
	UPROPERTY(Transient)
	TObjectPtr<APawn> pendingNewPawn;

	bool bDeathCamActive = false;
	bool bStopRequested = false;

	// DeathCam 활성 중 다른 카메라 로직이 ViewTarget을 빼앗는지 확인하기 위한 시점입니다.
	double enforceViewTargetAfterTime = 0.0;

	FTimerHandle stopRetryTimerHandle;
	FTimerHandle destroyAfterBlendTimerHandle;

	UFUNCTION(Client, Reliable)
	void ClientStartDeathCam(FVector deathLocation, AActor* otherActor, AActor* deadActorToIgnore);

	UFUNCTION(Client, Reliable)
	void ClientStopDeathCam(APawn* newPawn);

	APlayerController* GetOwningPlayerController();
	AActor* ResolveOtherActor(APawn* deadPawn, AController* instigatedBy, AActor* damageCauser) const;

	void StartDeathCamLocal(const FVector& deathLocation, AActor* otherActor, AActor* deadActorToIgnore);
	void RequestStopDeathCamLocal(APawn* newPawn);
	void TryCompleteStopDeathCam();
	void FinishStopDeathCam();
	void DestroyDeathCamImmediately();

	/** 현재 값은 부활 시 DeathCam -> 새 Pawn 복귀 Blend 시간으로만 사용합니다. */
	float GetViewTargetBlendTime() const;
};
