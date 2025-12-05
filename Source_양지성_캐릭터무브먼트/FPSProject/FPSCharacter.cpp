#include "FPSCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

AFPSCharacter::AFPSCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    // 카메라
    FPSCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FPSCameraComponent->SetupAttachment(GetCapsuleComponent());
    FPSCameraComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f + BaseEyeHeight));
    FPSCameraComponent->bUsePawnControlRotation = true;

    // 일인칭 메시
    FPSMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
    FPSMesh->SetOnlyOwnerSee(true);
    FPSMesh->SetupAttachment(FPSCameraComponent);
    FPSMesh->bCastDynamicShadow = false;
    FPSMesh->CastShadow = false;

    if (GetMesh())
    {
        GetMesh()->SetOwnerNoSee(true);
    }

    // 이동 기본값
    GetCharacterMovement()->JumpZVelocity = 600.f;
    GetCharacterMovement()->AirControl = 0.2f;

    // 더블 점프 허용
    JumpMaxCount = 2;
}

void AFPSCharacter::BeginPlay()
{
    Super::BeginPlay();
    JumpMaxCount = 2; // 블루프린트 덮어쓰기 방지

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, TEXT("FPSCharacter Ready!"));
    }
}

void AFPSCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void AFPSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    PlayerInputComponent->BindAxis("MoveForward", this, &AFPSCharacter::MoveForward);
    PlayerInputComponent->BindAxis("MoveRight", this, &AFPSCharacter::MoveRight);

    PlayerInputComponent->BindAxis("Turn", this, &AFPSCharacter::AddControllerYawInput);
    PlayerInputComponent->BindAxis("LookUp", this, &AFPSCharacter::AddControllerPitchInput);

    PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &AFPSCharacter::StartJump);
    PlayerInputComponent->BindAction("Jump", IE_Released, this, &AFPSCharacter::StopJump);

    PlayerInputComponent->BindAction("Dash", IE_Pressed, this, &AFPSCharacter::Dash);
}

void AFPSCharacter::MoveForward(float Value)
{
    if (Controller && Value != 0.0f)
    {
        const FVector Direction = FRotationMatrix(Controller->GetControlRotation()).GetScaledAxis(EAxis::X);
        AddMovementInput(Direction, Value);
    }
}

void AFPSCharacter::MoveRight(float Value)
{
    if (Controller && Value != 0.0f)
    {
        const FVector Direction = FRotationMatrix(Controller->GetControlRotation()).GetScaledAxis(EAxis::Y);
        AddMovementInput(Direction, Value);
    }
}

void AFPSCharacter::StartJump()
{
    Jump();
}

void AFPSCharacter::StopJump()
{
    StopJumping();
}

void AFPSCharacter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);
}

void AFPSCharacter::Dash()
{
    if (!bCanDash) return;

    const FVector ForwardDir = GetActorForwardVector();
    FVector DashVecRaw(ForwardDir.X, ForwardDir.Y, 0.f);
    FVector DashVec = DashVecRaw.GetSafeNormal();

    float ActualDashDistance = GetCharacterMovement()->IsMovingOnGround() ? 3000.f : 1000.f;

    LaunchCharacter(DashVec * ActualDashDistance, true, false);

    bCanDash = false;
    GetWorld()->GetTimerManager().SetTimer(DashTimerHandle, this, &AFPSCharacter::ResetDash, DashCooldown, false);
}

void AFPSCharacter::ResetDash()
{
    bCanDash = true;
}