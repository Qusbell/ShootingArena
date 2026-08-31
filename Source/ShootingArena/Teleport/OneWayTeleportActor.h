#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OneWayTeleportActor.generated.h"

class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTeleportDataAsset;
class ATargetPoint;
class ACharacter;

/**
 * 기획서 기준 단방향 텔레포트 Actor입니다.
 *
 * - EntryCollision에 Character가 진입하면 발동
 * - exitTarget 위치로 즉시 이동
 * - exitTarget Yaw를 출구 정면으로 사용
 * - DataAsset의 launchAngle / launchPower로 Launch
 * - DataAsset의 moveLockTime 동안 입력 제한
 * - PortalVisual로 충돌 영역을 반투명 표시
 * - EditorSelectionHandle로 에디터에서 쉽게 선택 가능
 */
UCLASS(Blueprintable)
class SHOOTINGARENA_API AOneWayTeleportActor : public AActor
{
	GENERATED_BODY()

public:
	AOneWayTeleportActor();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// ---------------------------------------------------------------------
	// Components
	// ---------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleport|Components")
	TObjectPtr<USceneComponent> root;

	/** 실제 텔레포트 진입 판정용 Collision입니다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleport|Components")
	TObjectPtr<UBoxComponent> entryCollision;

	/**
	 * EntryCollision 영역을 보여주는 반투명 Cube입니다.
	 * Collision / Navigation에는 영향을 주지 않습니다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Teleport|Components")
	TObjectPtr<UStaticMeshComponent> portalVisual;

#if WITH_EDITORONLY_DATA
	/**
	 * 에디터에서 Portal Actor를 쉽게 클릭하기 위한 선택용 구체입니다.
	 * 실제 게임/패키징에는 포함되지 않습니다.
	 */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> editorSelectionHandle;
#endif

	// ---------------------------------------------------------------------
	// Teleport Settings
	// ---------------------------------------------------------------------

	/** 레벨 인스턴스에서는 사실상 이 값만 지정합니다. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Teleport")
	TObjectPtr<ATargetPoint> exitTarget;

	/** BP 자식 Class Defaults에서 한 번 지정합니다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teleport")
	TObjectPtr<UTeleportDataAsset> teleportDA;

	// ---------------------------------------------------------------------
	// Visual Settings
	// ---------------------------------------------------------------------

	/**
	 * 체크: 에디터 + 실제 인게임에서 PortalVisual 표시
	 * 체크 해제: 에디터에서만 표시하고 실제 인게임에서는 숨김
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teleport|Visual")
	bool bShowPortalInGame = true;

	/** PortalVisual의 표시 색상입니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teleport|Visual")
	FLinearColor portalColor = FLinearColor(0.0f, 0.5f, 1.0f, 1.0f);

	/** 0 = 완전 투명, 1 = 완전 불투명 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teleport|Visual",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float portalOpacity = 0.25f;

	/**
	 * Color(Vector) / Opacity(Scalar) 파라미터를 가진 Translucent Material을 지정합니다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Teleport|Visual")
	TObjectPtr<UMaterialInterface> portalVisualMaterial;

	// ---------------------------------------------------------------------
	// Overlap
	// ---------------------------------------------------------------------

	UFUNCTION()
	void OnEntryBeginOverlap(
		UPrimitiveComponent* overlappedComponent,
		AActor* otherActor,
		UPrimitiveComponent* otherComp,
		int32 otherBodyIndex,
		bool bFromSweep,
		const FHitResult& sweepResult);

	// ---------------------------------------------------------------------
	// Core
	// ---------------------------------------------------------------------

	void TeleportCharacter(ACharacter* character);
	FRotator GetExitFacingRotation() const;
	FVector GetLaunchVelocity() const;

	/**
	 * 프로젝트 기존 입력 잠금 시스템이 있으면 BP 자식에서 Override할 수 있습니다.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Teleport")
	void ApplyMoveLock(ACharacter* character, float duration);

	virtual void ApplyMoveLock_Implementation(ACharacter* character, float duration);

private:
	/** PortalVisual 색/투명도 갱신용 MID */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> portalVisualMID;

	void UpdatePortalVisual();
};
