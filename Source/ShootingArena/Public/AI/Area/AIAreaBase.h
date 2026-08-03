#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AIAreaBase.generated.h"

class USceneComponent;
class UBoxComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * 월드의 한 전투 구역을 나타내는 기본 Actor입니다.
 * 위험도는 Manager/Service가 연결 수와 AI별 인식 정보를 사용해 계산하고,
 * 이 Actor는 구역 범위와 식별 정보만 보관합니다.
 */
UCLASS(Blueprintable)
class SHOOTINGARENA_API AAIAreaBase : public AActor
{
    GENERATED_BODY()

public:
    AAIAreaBase();

    /**
     * 에디터에서 Area를 배치하거나 Construction이 다시 실행될 때
     * World Outliner의 Display Name을 Area Name에 자동 반영합니다.
     */
    virtual void OnConstruction(const FTransform& Transform) override;

    /** 새로 배치되거나 Spawn된 Actor의 에디터 이름 동기화를 준비합니다. */
    virtual void PostActorCreated() override;

    /** 기존 맵에서 로드된 Area도 현재 Display Name으로 동기화합니다. */
    virtual void PostLoad() override;

    /** 등록한 에디터 이름 변경 이벤트를 안전하게 해제합니다. */
    virtual void BeginDestroy() override;

    /** 월드 위치가 이 Area Box 안에 포함되는지 검사합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area")
    bool ContainsPosition(const FVector& WorldPosition) const;

    /** Area Box의 월드 중심점을 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area")
    FVector GetAreaCenter() const;

    /** Area Box의 월드 Extent를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area")
    FVector GetAreaExtent() const;

    /** 입력 위치와 가장 가까우면서 Area 안쪽에 있는 위치를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area")
    FVector GetClosestPointInArea(const FVector& WorldPosition, float Inset = 0.0f) const;

    /** 목표 방향의 Area 내부 지점을 반환합니다. 자동 Normal 연결의 진입/도착점 생성에 사용합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area")
    FVector GetPointInsideTowards(const FVector& WorldTarget, float Inset = 80.0f) const;

    UFUNCTION(BlueprintPure, Category = "AI|Area")
    FName GetAreaId() const { return AreaId; }

    UFUNCTION(BlueprintPure, Category = "AI|Area")
    bool IsAreaEnabled() const { return bAreaEnabled; }

    /** C++ 내부 그래프 계산용 월드 Bounds입니다. */
    FBox GetAreaBounds() const;

    UBoxComponent* GetAreaBoxComponent() const { return AreaBounds; }

#if WITH_EDITOR
    /** 에디터 속성이 갱신될 때 Area Name도 다시 확인합니다. */
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

    /** 복사/붙여넣기 또는 복제된 Area의 새 Display Name을 Area Name에 반영합니다. */
    virtual void PostEditImport() override;

    /** 이름 변경을 Undo/Redo했을 때 Area Name을 다시 동기화합니다. */
    virtual void PostEditUndo() override;
#endif

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Area|Components")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Area|Components")
    TObjectPtr<UBoxComponent> AreaBounds;

#if WITH_EDITORONLY_DATA
    /**
     * AreaBounds의 실제 월드 크기를 그대로 보여주는 에디터 전용 Preview입니다.
     *
     * BP Construction Script의 하드코딩된 Cube 크기 가정이나
     * 부모 컴포넌트 Scale에 의존하지 않도록 C++에서 직접 맞춥니다.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Area|Debug")
    TObjectPtr<UStaticMeshComponent> AreaDebugPreview;

    /**
     * AreaDebugPreview에 사용할 반투명 Material입니다.
     *
     * Material Parameter:
     * - Vector: areaColor
     * - Scalar: previewOpacity
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Area|Debug")
    TObjectPtr<UMaterialInterface> AreaDebugMaterial = nullptr;

    /** 맵에 배치된 Area마다 지정할 Preview 색입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Debug")
    FLinearColor AreaDebugColor =
        FLinearColor(0.0f, 0.45f, 1.0f, 1.0f);

    /** Preview Material의 previewOpacity 값입니다. */
    UPROPERTY(
        EditInstanceOnly,
        BlueprintReadOnly,
        Category = "AI|Area|Debug",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AreaDebugOpacity = 0.12f;

    /** Construction이 반복되어도 Dynamic Material을 GC로부터 유지합니다. */
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> AreaDebugMID = nullptr;
#endif

    /**
     * World Outliner의 Display Name에서 자동 생성되는 Area 식별자입니다.
     * 패키징 빌드에서도 사용할 수 있도록 에디터에서 값이 저장됩니다.
     */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "AI|Area", meta = (DisplayName = "Area Name"))
    FName AreaId = NAME_None;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area")
    bool bAreaEnabled = true;

private:
#if WITH_EDITOR
    /** AreaBounds와 AreaDebugPreview의 월드 위치/회전/크기를 정확히 동기화합니다. */
    void UpdateAreaDebugPreview();

    /** Actor Label 변경 전역 이벤트에 이 Area를 등록합니다. */
    void BindActorLabelChangedDelegate();

    /** Actor Label 변경 전역 이벤트 등록을 해제합니다. */
    void UnbindActorLabelChangedDelegate();

    /** 이 Actor의 World Outliner Display Name이 변경됐을 때 호출됩니다. */
    void HandleActorLabelChanged(AActor* ChangedActor);

    /** 현재 Actor Display Name을 AreaId에 복사합니다. */
    void SyncAreaIdFromActorLabel();

    /** 전역 Actor Label 변경 이벤트 등록 핸들입니다. */
    FDelegateHandle ActorLabelChangedHandle;
#endif
};
