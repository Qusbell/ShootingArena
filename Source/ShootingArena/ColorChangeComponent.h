// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ColorChangeComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class SHOOTINGARENA_API UColorChangeComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UColorChangeComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// 에디터에서 변경할 목표 색상
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor m_TargetColor;

	// Material의 파라미터 이름 (BaseColor로 설정함)
	UPROPERTY(EditAnywhere, Category="Appearance")
	FName m_ParameterName;

	// 색상을 실제로 변경하는 함수
	UFUNCTION(BlueprintCallable, Category="Appearance")
	void ApplyColorChange();

	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

		
};
