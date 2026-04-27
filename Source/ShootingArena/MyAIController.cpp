#include "MyAIController.h"
#include "AISystem.h"

void AMyAIController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
    APawn* const MyPawn = GetPawn();
    if (MyPawn)
    {
        FRotator NewControlRotation = GetControlRotation();

        const FVector FocalPoint = GetFocalPoint();
        if (FAISystem::IsValidLocation(FocalPoint))
        {
            NewControlRotation = (FocalPoint - MyPawn->GetPawnViewLocation()).Rotation();
        }
        else if (bSetControlRotationFromPawnOrientation)
        {
            NewControlRotation = MyPawn->GetActorRotation();
        }


        SetControlRotation(NewControlRotation);

        if (bUpdatePawn)
        {
            const FRotator CurrentPawnRotation = MyPawn->GetActorRotation();
            if (CurrentPawnRotation.Equals(NewControlRotation, 1e-3f) == false)
            {
                MyPawn->FaceRotation(NewControlRotation, DeltaTime);
            }
        }
    }
}