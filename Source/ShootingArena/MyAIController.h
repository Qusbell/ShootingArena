// Fill out your copyright notice in the Description page of Project Settings.
#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "MyAIController.generated.h"

UCLASS()
class SHOOTINGARENA_API AMyAIController : public AAIController
{
    GENERATED_BODY()

public:
    virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;
};