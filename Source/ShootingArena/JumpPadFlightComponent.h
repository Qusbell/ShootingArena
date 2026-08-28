// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JumpPadFlightComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;

/**
 * 점프패드로 발사된 캐릭터가 착지할 때까지
 * 상승 구간(Vz > 0)과 하강 구간(Vz <= 0)에 서로 다른 GravityScale 을 적용한다.
 * 발사 속도 계산은 UCustomWrapperLibrary::SuggestJumpPadVelocity 와 짝을 이룬다.
 *
 * 사용 흐름:
 *   1) 점프패드에서 SuggestJumpPadVelocity 로 발사 속도 계산
 *   2) 이 컴포넌트의 BeginFlight(RiseScale, FallScale) 호출 (같은 값)
 *   3) Character->LaunchCharacter(Velocity, true, true)
 *   4) 착지 시 자동으로 원래 GravityScale 복원
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SHOOTINGARENA_API UJumpPadFlightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJumpPadFlightComponent();

	/**
	 * 점프패드 발사 직후 호출. 착지(OnLanded)까지
	 * Vz > 0 이면 RiseGravityScale, 그 외에는 FallGravityScale 을 매 틱 적용한다.
	 * SuggestJumpPadVelocity 에 넘긴 것과 같은 값을 넘길 것.
	 */
	UFUNCTION(BlueprintCallable, Category = "JumpPad")
	void BeginFlight(float RiseGravityScale, float FallGravityScale);

	/** 비행 강제 종료 + GravityScale 원복. */
	UFUNCTION(BlueprintCallable, Category = "JumpPad")
	void EndFlight();

	UFUNCTION(BlueprintPure, Category = "JumpPad")
	bool IsInFlight() const { return bInFlight; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<ACharacter> OwnerCharacter;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> MoveComp;

	float RiseScale = 1.0f;
	float FallScale = 1.0f;
	float OrigGravityScale = 1.0f;
	bool bInFlight = false;

	UFUNCTION()
	void HandleLanded(const FHitResult& Hit);
};
