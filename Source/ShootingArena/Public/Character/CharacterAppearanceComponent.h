#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "CharacterAppearanceComponent.generated.h"

class UAnimInstance;
class USkeletalMesh;
class USkeletalMeshComponent;

/** 다중 파츠 사망 캐릭터에서 컴포넌트별로 교체할 Skeletal Mesh입니다. */
USTRUCT(BlueprintType)
struct FCharacterDeathMeshPart
{
	GENERATED_BODY()

	/** BP Components 패널에 표시되는 Skeletal Mesh Component 이름입니다. 예: SKM_Leg_L1 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
	FName componentName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
	TSoftObjectPtr<USkeletalMesh> skeletalMesh;
};

/**
 * 캐릭터 DataTable의 Row입니다.
 * 캐릭터는 모델 자체를 직접 보관하지 않고 modelingId로 모델링 DataTable을 참조합니다.
 */
USTRUCT(BlueprintType)
struct FCharacterAppearanceRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
	FString modelingId;
};

/**
 * 모델링 DataTable의 Row입니다.
 * 일반 메시와 사망 메시를 한 레코드에 묶어, 사망 시에도 현재 선택 캐릭터의 외형을 사용합니다.
 */
USTRUCT(BlueprintType)
struct FCharacterModelingRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
	FString modelingId;

	/** 평상시 Character Mesh Component에 적용할 메시입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
	TSoftObjectPtr<USkeletalMesh> characterMesh;

	/** 단일 Skeletal Mesh 사망 구조에서 Death Mesh Component에 적용할 메시입니다. 비어 있으면 characterMesh를 사용합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
	TSoftObjectPtr<USkeletalMesh> deathMesh;

	/**
	 * Death Meshes처럼 절단 가능한 다중 파츠 구조에서 사용하는 컴포넌트별 메시입니다.
	 * 비어 있으면 BP에 이미 배치된 기존 파츠 메시를 보존합니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
	TArray<FCharacterDeathMeshPart> deathMeshParts;

	/** 일반 메시의 Animation Blueprint입니다. 캐릭터별 Skeleton이 다를 때 함께 지정합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance")
	TSoftClassPtr<UAnimInstance> animationInstanceClass;
};

/**
 * 캐릭터 외형과 사망 외형을 DataTable 기반으로 동기화합니다.
 *
 * BP에는 이 컴포넌트를 추가하고 DataTable/메시 컴포넌트만 지정하면 BeginPlay에 자동 적용됩니다.
 * 기존 사망 그래프는 Death Mesh Component를 표시하고 Ragdoll을 적용하는 흐름을 그대로 사용합니다.
 */
UCLASS(ClassGroup = (Character), meta = (BlueprintSpawnableComponent))
class SHOOTINGARENA_API UCharacterAppearanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCharacterAppearanceComponent();

	/** 캐릭터의 DataTable Row. 이 Row의 modelingId로 모델링 DataTable을 조회합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance|Data")
	FDataTableRowHandle characterDataRow;

	/** modelingId -> 일반/사망 메시를 보관하는 DataTable입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance|Data")
	TObjectPtr<UDataTable> modelingDataTable = nullptr;

	/** 일반 캐릭터 메시 컴포넌트입니다. 비워두면 소유 Actor의 첫 SkeletalMeshComponent를 사용합니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance|Components", meta = (UseComponentPicker, AllowAnyActor = "false"))
	FComponentReference characterMeshComponent;

	/**
	 * 기존 사망 그래프에서 표시하는 Death Mesh입니다.
	 * 비워도 Component Tag/이름(DeathMesh, Death Mesh)을 자동 탐색하고, 해당 Scene Component의 Skeletal Mesh 자식에도 적용합니다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Appearance|Components", meta = (UseComponentPicker, AllowAnyActor = "false"))
	FComponentReference deathMeshComponent;

	/** 현재 적용한 모델링 ID입니다. 서버에서 변경되면 모든 Client가 같은 외형을 적용합니다. */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveModelingId, VisibleAnywhere, BlueprintReadOnly, Category = "Character Appearance|Runtime")
	FString activeModelingId;

	/** characterDataRow에서 모델링 ID를 읽고 일반/사망 메시를 모두 미리 적용합니다. */
	UFUNCTION(BlueprintCallable, Category = "Character Appearance")
	bool ApplyConfiguredAppearance();

	/** 선택/스폰 로직에서 모델링 ID를 직접 지정해야 할 때 사용합니다. Server에서 호출해야 합니다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Character Appearance")
	bool SetModelingId(const FString& newModelingId);

	/** 현재 모델링의 사망 메시를 Death Mesh Component에 다시 적용합니다. 사망 직전에 호출해도 됩니다. */
	UFUNCTION(BlueprintCallable, Category = "Character Appearance")
	bool ApplyDeathAppearance();

	/** 현재 모델링 ID에 해당하는 Row를 반환합니다. */
	UFUNCTION(BlueprintPure, Category = "Character Appearance")
	bool GetActiveModelingRow(FCharacterModelingRow& outModelingRow) const;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& outLifetimeProps) const override;

private:
	UFUNCTION()
	void OnRep_ActiveModelingId();

	bool ApplyModelingIdInternal(const FString& modelingId);
	bool FindModelingRow(const FString& modelingId, FCharacterModelingRow& outModelingRow) const;
	USkeletalMeshComponent* ResolveCharacterMeshComponent() const;
	TArray<USkeletalMeshComponent*> ResolveDeathMeshComponents() const;
	void ApplyLiveMesh(const FCharacterModelingRow& modelingRow) const;
	void ApplyDeathMesh(const FCharacterModelingRow& modelingRow) const;
	void ApplyDeathPartMaterials(USkeletalMesh* sourceMesh, const TArray<USkeletalMeshComponent*>& meshComponents) const;
};
