#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Camera/CameraComponent.h"
#include "FPSCharacter.generated.h"

UCLASS(Blueprintable)
class FPSPROJECT_API AFPSCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AFPSCharacter();

protected:
    virtual void BeginPlay() override;

    // 착지 이벤트 오버라이드 (점프 카운트 초기화)
    virtual void Landed(const FHitResult& Hit) override;

public:
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    // ---------- 컴포넌트 ----------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCameraComponent* FPSCameraComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USkeletalMeshComponent* FPSMesh;

    // ---------- 이동 / 점프 ----------
    UFUNCTION(BlueprintCallable, Category = "Movement")
    void MoveForward(float Value);

    UFUNCTION(BlueprintCallable, Category = "Movement")
    void MoveRight(float Value);

    UFUNCTION(BlueprintCallable, Category = "Movement")
    void StartJump();

    UFUNCTION(BlueprintCallable, Category = "Movement")
    void StopJump();

    // ---------- 대쉬 ----------
    UFUNCTION(BlueprintCallable, Category = "Movement")
    void Dash();

    void ResetDash();

    // 지상 대쉬 거리
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float GroundDashDistance = 4000.0f;

    // 공중 대쉬 거리
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float AirDashDistance = 1000.0f;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    float DashCooldown = 1.0f;

    bool bCanDash = true;

    FTimerHandle DashTimerHandle; // 대쉬 쿨다운 관리용

    // ---------- 더블 점프 ----------
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
    int MaxJumpCount = 2;

    int JumpCount = 0;
};