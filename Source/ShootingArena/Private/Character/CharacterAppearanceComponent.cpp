#include "Character/CharacterAppearanceComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCharacterAppearance, Log, All);

UCharacterAppearanceComponent::UCharacterAppearanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCharacterAppearanceComponent::BeginPlay()
{
	Super::BeginPlay();

	// DataTable 설정이 모든 Client에서 동일한 경우, 로컬에서도 즉시 적용해 메시 로딩 지연을 줄입니다.
	if (!activeModelingId.IsEmpty())
	{
		ApplyModelingIdInternal(activeModelingId);
		return;
	}

	ApplyConfiguredAppearance();
}

void UCharacterAppearanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCharacterAppearanceComponent, activeModelingId);
}

bool UCharacterAppearanceComponent::ApplyConfiguredAppearance()
{
	const FCharacterAppearanceRow* characterRow = characterDataRow.GetRow<FCharacterAppearanceRow>(TEXT("Character appearance initialization"));
	if (characterRow == nullptr || characterRow->modelingId.IsEmpty())
	{
		UE_LOG(LogCharacterAppearance, Warning, TEXT("CharacterDataRow 또는 modelingId가 설정되지 않았습니다. Owner=%s"), *GetNameSafe(GetOwner()));
		return false;
	}

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		activeModelingId = characterRow->modelingId;
	}

	return ApplyModelingIdInternal(characterRow->modelingId);
}

bool UCharacterAppearanceComponent::SetModelingId(const FString& newModelingId)
{
	if (newModelingId.IsEmpty())
	{
		UE_LOG(LogCharacterAppearance, Warning, TEXT("빈 modelingId는 적용할 수 없습니다. Owner=%s"), *GetNameSafe(GetOwner()));
		return false;
	}

	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		UE_LOG(LogCharacterAppearance, Warning, TEXT("SetModelingId는 Server에서 호출해야 합니다. Owner=%s"), *GetNameSafe(GetOwner()));
		return false;
	}

	activeModelingId = newModelingId;
	return ApplyModelingIdInternal(activeModelingId);
}

bool UCharacterAppearanceComponent::ApplyDeathAppearance()
{
	FCharacterModelingRow modelingRow;
	if (!GetActiveModelingRow(modelingRow))
	{
		return false;
	}

	ApplyDeathMesh(modelingRow);
	return true;
}

bool UCharacterAppearanceComponent::GetActiveModelingRow(FCharacterModelingRow& outModelingRow) const
{
	FString modelingIdToResolve = activeModelingId;
	if (modelingIdToResolve.IsEmpty())
	{
		const FCharacterAppearanceRow* characterRow = characterDataRow.GetRow<FCharacterAppearanceRow>(TEXT("Character appearance lookup"));
		modelingIdToResolve = characterRow != nullptr ? characterRow->modelingId : FString();
	}

	return FindModelingRow(modelingIdToResolve, outModelingRow);
}

void UCharacterAppearanceComponent::OnRep_ActiveModelingId()
{
	ApplyModelingIdInternal(activeModelingId);
}

bool UCharacterAppearanceComponent::ApplyModelingIdInternal(const FString& modelingId)
{
	FCharacterModelingRow modelingRow;
	if (!FindModelingRow(modelingId, modelingRow))
	{
		return false;
	}

	ApplyLiveMesh(modelingRow);
	ApplyDeathMesh(modelingRow);
	return true;
}

bool UCharacterAppearanceComponent::FindModelingRow(const FString& modelingId, FCharacterModelingRow& outModelingRow) const
{
	if (modelingId.IsEmpty() || modelingDataTable == nullptr)
	{
		UE_LOG(LogCharacterAppearance, Warning, TEXT("modelingId 또는 ModelingDataTable이 설정되지 않았습니다. Owner=%s"), *GetNameSafe(GetOwner()));
		return false;
	}

	const TArray<FName> rowNames = modelingDataTable->GetRowNames();
	for (const FName& rowName : rowNames)
	{
		const FCharacterModelingRow* row = modelingDataTable->FindRow<FCharacterModelingRow>(rowName, TEXT("Character appearance lookup"), false);
		if (row != nullptr && row->modelingId.Equals(modelingId, ESearchCase::IgnoreCase))
		{
			outModelingRow = *row;
			return true;
		}
	}

	UE_LOG(LogCharacterAppearance, Warning, TEXT("ModelingDataTable에서 modelingId '%s'를 찾지 못했습니다. Owner=%s"), *modelingId, *GetNameSafe(GetOwner()));
	return false;
}

USkeletalMeshComponent* UCharacterAppearanceComponent::ResolveCharacterMeshComponent() const
{
	if (UActorComponent* component = characterMeshComponent.GetComponent(GetOwner()))
	{
		if (USkeletalMeshComponent* meshComponent = Cast<USkeletalMeshComponent>(component))
		{
			return meshComponent;
		}
	}

	// BP_PlayerShooter처럼 ACharacter를 상속한 BP에서는 엔진이 관리하는 기본 Mesh를 우선 사용합니다.
	// FindComponentByClass 결과가 BP 구성 순서에 영향을 받지 않도록 하는 안전한 경로입니다.
	if (const ACharacter* characterOwner = Cast<ACharacter>(GetOwner()))
	{
		return characterOwner->GetMesh();
	}

	return GetOwner() != nullptr ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

TArray<USkeletalMeshComponent*> UCharacterAppearanceComponent::ResolveDeathMeshComponents() const
{
	TArray<USkeletalMeshComponent*> resolvedComponents;
	AActor* owner = GetOwner();
	if (owner == nullptr)
	{
		return resolvedComponents;
	}

	TSet<USkeletalMeshComponent*> uniqueComponents;
	auto AddSkeletalMesh = [&uniqueComponents, &resolvedComponents](USkeletalMeshComponent* meshComponent)
	{
		if (meshComponent != nullptr && !uniqueComponents.Contains(meshComponent))
		{
			uniqueComponents.Add(meshComponent);
			resolvedComponents.Add(meshComponent);
		}
	};

	auto AddSkeletalMeshChildren = [&AddSkeletalMesh](USceneComponent* sceneComponent)
	{
		if (sceneComponent == nullptr)
		{
			return;
		}

		AddSkeletalMesh(Cast<USkeletalMeshComponent>(sceneComponent));

		TArray<USceneComponent*> childComponents;
		sceneComponent->GetChildrenComponents(true, childComponents);
		for (USceneComponent* childComponent : childComponents)
		{
			AddSkeletalMesh(Cast<USkeletalMeshComponent>(childComponent));
		}
	};

	if (UActorComponent* configuredComponent = deathMeshComponent.GetComponent(owner))
	{
		AddSkeletalMeshChildren(Cast<USceneComponent>(configuredComponent));
	}

	if (!resolvedComponents.IsEmpty())
	{
		return resolvedComponents;
	}

	auto IsDeathMeshCandidate = [](const UActorComponent* component)
	{
		if (component == nullptr)
		{
			return false;
		}

		if (component->ComponentTags.Contains(TEXT("DeathMesh")) || component->ComponentTags.Contains(TEXT("Death")))
		{
			return true;
		}

		FString componentName = component->GetName();
		componentName.ReplaceInline(TEXT(" "), TEXT(""));
		componentName.ToLowerInline();
		return componentName.Contains(TEXT("death"));
	};

	// 기존 Blueprint의 "Death Mesh" 부모 Scene Component와 그 자식 Skeletal Mesh를 자동 탐색합니다.
	TArray<USceneComponent*> sceneComponents;
	owner->GetComponents<USceneComponent>(sceneComponents);
	for (USceneComponent* sceneComponent : sceneComponents)
	{
		if (IsDeathMeshCandidate(sceneComponent))
		{
			AddSkeletalMeshChildren(sceneComponent);
		}
	}

	return resolvedComponents;
}

void UCharacterAppearanceComponent::ApplyLiveMesh(const FCharacterModelingRow& modelingRow) const
{
	USkeletalMeshComponent* meshComponent = ResolveCharacterMeshComponent();
	if (meshComponent == nullptr)
	{
		UE_LOG(LogCharacterAppearance, Warning, TEXT("일반 Character Mesh Component를 찾지 못했습니다. Owner=%s"), *GetNameSafe(GetOwner()));
		return;
	}

	if (USkeletalMesh* mesh = modelingRow.characterMesh.LoadSynchronous())
	{
		meshComponent->SetSkeletalMesh(mesh);
	}

	if (!modelingRow.animationInstanceClass.IsNull())
	{
		if (UClass* animClass = modelingRow.animationInstanceClass.LoadSynchronous())
		{
			meshComponent->SetAnimInstanceClass(animClass);
		}
	}
}

void UCharacterAppearanceComponent::ApplyDeathMesh(const FCharacterModelingRow& modelingRow) const
{
	const TArray<USkeletalMeshComponent*> meshComponents = ResolveDeathMeshComponents();
	if (meshComponents.IsEmpty())
	{
		UE_LOG(LogCharacterAppearance, Warning,
			TEXT("Death Mesh SkeletalMeshComponent를 찾지 못했습니다. Component Tag를 DeathMesh로 지정하거나 이름에 Death를 포함하세요. Owner=%s"),
			*GetNameSafe(GetOwner()));
		return;
	}

	if (!modelingRow.deathMeshParts.IsEmpty())
	{
		for (const FCharacterDeathMeshPart& deathMeshPart : modelingRow.deathMeshParts)
		{
			if (deathMeshPart.componentName.IsNone())
			{
				continue;
			}

			USkeletalMesh* partMesh = deathMeshPart.skeletalMesh.LoadSynchronous();
			if (partMesh == nullptr)
			{
				continue;
			}

			for (USkeletalMeshComponent* meshComponent : meshComponents)
			{
				if (meshComponent != nullptr && meshComponent->GetFName() == deathMeshPart.componentName)
				{
					meshComponent->SetSkeletalMesh(partMesh);
					break;
				}
			}
		}

		return;
	}

	// 다중 파츠 사망 구조는 단일 전신 메시로 덮어쓰지 않습니다.
	// BP에 배치된 팔/다리/머리 파츠를 보존해야 기존 Ragdoll과 절단 연출이 유지됩니다.
	if (meshComponents.Num() != 1)
	{
		USkeletalMesh* materialSourceMesh = modelingRow.deathMesh.LoadSynchronous();
		if (materialSourceMesh == nullptr)
		{
			materialSourceMesh = modelingRow.characterMesh.LoadSynchronous();
		}

		ApplyDeathPartMaterials(materialSourceMesh, meshComponents);
		return;
	}

	USkeletalMesh* deathMesh = modelingRow.deathMesh.LoadSynchronous();
	if (deathMesh == nullptr)
	{
		deathMesh = modelingRow.characterMesh.LoadSynchronous();
	}

	if (deathMesh != nullptr && meshComponents[0] != nullptr)
	{
		meshComponents[0]->SetSkeletalMesh(deathMesh);
	}
}

void UCharacterAppearanceComponent::ApplyDeathPartMaterials(
	USkeletalMesh* sourceMesh,
	const TArray<USkeletalMeshComponent*>& meshComponents) const
{
	if (sourceMesh == nullptr)
	{
		UE_LOG(LogCharacterAppearance, Warning,
			TEXT("다중 Death Mesh 파츠에 적용할 Character/Death Mesh가 없습니다. Owner=%s"),
			*GetNameSafe(GetOwner()));
		return;
	}

	const TArray<FSkeletalMaterial>& sourceMaterials = sourceMesh->GetMaterials();
	auto NormalizeMaterialName = [](FString materialName)
	{
		materialName.ToLowerInline();
		materialName.ReplaceInline(TEXT("m_"), TEXT(""));
		materialName.ReplaceInline(TEXT("mic_"), TEXT(""));
		materialName.ReplaceInline(TEXT("steel"), TEXT(""));
		materialName.ReplaceInline(TEXT("doomsday"), TEXT(""));
		materialName.ReplaceInline(TEXT("_inst"), TEXT(""));
		materialName.ReplaceInline(TEXT("_noshine"), TEXT(""));
		materialName.ReplaceInline(TEXT("_001"), TEXT(""));
		materialName.ReplaceInline(TEXT("_004"), TEXT(""));
		return materialName;
	};

	auto FindSourceMaterialByToken = [&sourceMaterials, &NormalizeMaterialName](const FString& token) -> UMaterialInterface*
	{
		for (const FSkeletalMaterial& sourceMaterial : sourceMaterials)
		{
			if (NormalizeMaterialName(sourceMaterial.MaterialSlotName.ToString()).Contains(token))
			{
				return sourceMaterial.MaterialInterface;
			}
		}

		return nullptr;
	};

	for (USkeletalMeshComponent* meshComponent : meshComponents)
	{
		if (meshComponent == nullptr)
		{
			continue;
		}

		const TArray<FName> targetSlotNames = meshComponent->GetMaterialSlotNames();
		for (int32 targetIndex = 0; targetIndex < targetSlotNames.Num(); ++targetIndex)
		{
			const FName targetSlotName = targetSlotNames[targetIndex];
			const FString targetName = NormalizeMaterialName(targetSlotName.ToString());
			FString componentName = meshComponent->GetName();
			componentName.ToLowerInline();
			UMaterialInterface* matchedMaterial = nullptr;

			for (const FSkeletalMaterial& sourceMaterial : sourceMaterials)
			{
				if (sourceMaterial.MaterialSlotName == targetSlotName || sourceMaterial.ImportedMaterialSlotName == targetSlotName)
				{
					matchedMaterial = sourceMaterial.MaterialInterface;
					break;
				}
			}

			// Doomsday처럼 전신 메시와 기존 절단 파츠의 슬롯 이름이 다른 스킨을 보정합니다.
			// 파츠의 역할(몸통/팔/다리)과 대상 슬롯의 의미를 기준으로 전신 스킨의 대응 머티리얼을 선택합니다.
			if (matchedMaterial == nullptr)
			{
				FString semanticToken;
				if (targetName.Contains(TEXT("tearline")))
				{
					semanticToken = TEXT("tearline");
				}
				else if (targetName.Contains(TEXT("tearduct")))
				{
					semanticToken = TEXT("tearduct");
				}
				else if (targetName.Contains(TEXT("shadow")))
				{
					semanticToken = TEXT("eyeshadow");
				}
				else if (targetName.Contains(TEXT("eye")))
				{
					semanticToken = TEXT("eye");
				}
				else if (componentName.Contains(TEXT("leg")) || targetName.Contains(TEXT("lowerbody")))
				{
					semanticToken = TEXT("legs");
				}
				else if (componentName.Contains(TEXT("hand")) || componentName.Contains(TEXT("arm")))
				{
					semanticToken = TEXT("arms");
				}
				else if (targetName.Contains(TEXT("upperbody")) || targetName.Contains(TEXT("skin")) || componentName.Contains(TEXT("body")) || componentName.Contains(TEXT("head")))
				{
					semanticToken = TEXT("torso");
				}

				if (!semanticToken.IsEmpty())
				{
					matchedMaterial = FindSourceMaterialByToken(semanticToken);
				}
			}

			// 슬롯이 하나뿐인 파츠와 전신 메시가 단일 머티리얼인 경우에는 이름이 달라도 안전하게 적용합니다.
			if (matchedMaterial == nullptr && targetSlotNames.Num() == 1 && sourceMaterials.Num() == 1)
			{
				matchedMaterial = sourceMaterials[0].MaterialInterface;
			}

			if (matchedMaterial != nullptr)
			{
				meshComponent->SetMaterial(targetIndex, matchedMaterial);
			}
		}
	}
}
