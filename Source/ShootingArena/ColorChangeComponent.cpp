// Fill out your copyright notice in the Description page of Project Settings.


#include "ColorChangeComponent.h"
#include "Components/MeshComponent.h" // 매쉬 컴포넌트에 접근하기 위해
#include "Materials/MaterialInstanceDynamic.h" // 동적 material 인스턴스

// Sets default values for this component's properties
UColorChangeComponent::UColorChangeComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false; // 틱을 우선 사용안하려고 끔

	// 기본값 설정
	m_TargetColor = FLinearColor::Red; //기본 빨강
	m_ParameterName = FName("BaseColor"); //material 파라미터 이름과 일치해야함
	// ...
}


// Called when the game starts
void UColorChangeComponent::BeginPlay()
{
	Super::BeginPlay();


	ApplyColorChange();
	// ...
	
}


void UColorChangeComponent::ApplyColorChange()
{
	AActor* Owner = GetOwner(); // 컴포넌트 주인 가져오기
	if (!Owner) return;

	UMeshComponent* MeshComp = Owner->FindComponentByClass<UMeshComponent>(); // unity에 find같은거임
	if (MeshComp)
	{
		// 0번 인덱스 기준으로 동적 머터리얼 생성
		UMaterialInstanceDynamic* DynMat = MeshComp->CreateAndSetMaterialInstanceDynamic(0);
		if (DynMat)
		{
			DynMat->SetVectorParameterValue(m_ParameterName, m_TargetColor);
		}
	}
}

// Called every frame
void UColorChangeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

