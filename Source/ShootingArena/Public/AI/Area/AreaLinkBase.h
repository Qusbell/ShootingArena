#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/Area/AreaTypes.h"
#include "AreaLinkBase.generated.h"

class USceneComponent;
class UArrowComponent;
class AAIAreaBase;
class UWorld;

/**
 * 두 Area 사이의 수동 연결을 나타냅니다.
 * 진입/도착 위치와 Area는 가능한 한 C++에서 자동 해석하고, 수동 Point는 보정용으로만 사용합니다.
 */
UCLASS(Blueprintable)
class SHOOTINGARENA_API AAreaLinkBase : public AActor
{
    GENERATED_BODY()

public:
    AAreaLinkBase();

    virtual void OnConstruction(const FTransform& Transform) override;

    /** Link 기본 설정과 Endpoint 설정이 사용 가능한지 검사합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    bool IsValidLink() const;

    /** 현재 설정으로 진입점과 도착점을 계산합니다. 실패 원인은 OutFailureReason으로 반환합니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Link")
    bool ResolveEndpointLocations(
        FVector& OutEntryLocation,
        FVector& OutExitLocation,
        FText& OutFailureReason) const;

    /** 참조 Actor가 이동한 뒤 에디터용 Arrow 미리보기를 새로 맞춥니다. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "AI|Area|Link|Editor")
    void RefreshEndpointPreview();

    /** C++ 그래프 빌드에서 사용하는 Endpoint 계산 함수입니다. */
    bool TryResolveEndpointLocations(
        UWorld* World,
        FVector& OutEntryLocation,
        FVector& OutExitLocation,
        FString& OutFailureReason) const;

    /** 값이 있으면 자동 Area 판정보다 우선하는 Area A 수동 Override입니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    AAIAreaBase* GetAreaA() const { return AreaA; }

    /** 값이 있으면 자동 Area 판정보다 우선하는 Area B 수동 Override입니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    AAIAreaBase* GetAreaB() const { return AreaB; }

    /** 기존 호출 호환용입니다. 정방향(A -> B) 이동 종류를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    EAreaTraversalType GetTraversalType() const { return GetTraversalTypeAtoB(); }

    /** Area A에서 Area B로 갈 때 사용하는 이동 종류입니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    EAreaTraversalType GetTraversalTypeAtoB() const { return TraversalType; }

    /**
     * Area B에서 Area A로 갈 때 사용하는 이동 종류입니다.
     * Jump와 Drop은 방향이 뒤집히면 서로 자동 변환됩니다.
     */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    EAreaTraversalType GetTraversalTypeBtoA() const;

    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    EAreaLinkEndpointMode GetEndpointMode() const { return EndpointMode; }

    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    bool IsTwoWay() const { return bTwoWay; }

    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    bool IsLinkEnabled() const { return bLinkEnabled; }

    /** 기존 호출 호환용입니다. 정방향(A -> B) 실행 Actor를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    AActor* GetTraversalActor() const { return GetTraversalActorAtoB(); }

    /** Area A에서 Area B로 이동할 때 팀 이동 시스템에 넘길 Goal Actor입니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    AActor* GetTraversalActorAtoB() const { return TraversalActor; }

    /**
     * Area B에서 Area A로 이동할 때 팀 이동 시스템에 넘길 Goal Actor입니다.
     * Reverse Traversal Actor가 비어 있으면 정방향 Actor를 재사용합니다.
     */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    AActor* GetTraversalActorBtoA() const
    {
        if (IsValid(ReverseTraversalActor))
        {
            return ReverseTraversalActor.Get();
        }

        // Jump와 Drop은 역방향에서 이동 종류가 바뀝니다.
        // 역방향 Actor가 없으면 정방향 Actor를 잘못 재사용하지 않고
        // B -> A Exit 위치 기반의 내부 목표 Actor를 사용하게 합니다.
        return GetTraversalTypeBtoA() == GetTraversalTypeAtoB()
            ? TraversalActor.Get()
            : nullptr;
    }

    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    AActor* GetEntryActor() const { return EntryActor; }

    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    AActor* GetExitActor() const { return ExitActor; }

    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    float GetAreaResolveTolerance() const { return AreaResolveTolerance; }

    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    float GetTraversalDistanceCost() const { return TraversalDistanceCost; }

    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    float GetTraversalRiskCost() const { return TraversalRiskCost; }

    /** A에서 B로 이동할 때의 시작 위치입니다. 계산 실패 시 수동 Point 위치를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    FVector GetEntryLocationAtoB() const;

    /** A에서 B로 이동한 뒤의 도착 위치입니다. 계산 실패 시 수동 Point 위치를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    FVector GetExitLocationAtoB() const;

    /** B에서 A로 이동할 때의 시작 위치입니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    FVector GetEntryLocationBtoA() const;

    /** B에서 A로 이동한 뒤의 도착 위치입니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Link")
    FVector GetExitLocationBtoA() const;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Area|Link|Components")
    TObjectPtr<USceneComponent> SceneRoot;

    /** 수동 Override와 에디터 미리보기에 사용하는 AreaA 쪽 Point입니다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Area|Link|Components")
    TObjectPtr<UArrowComponent> AreaAPoint;

    /** 수동 Override와 에디터 미리보기에 사용하는 AreaB 쪽 Point입니다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Area|Link|Components")
    TObjectPtr<UArrowComponent> AreaBPoint;

    /**
     * 비어 있으면 Entry 위치를 포함하는 Area를 C++에서 자동 판정합니다.
     * 특수한 맵 구조에서 자동 판정이 원하는 결과와 다를 때만 지정합니다.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Overrides", meta = (DisplayName = "Area A Override"))
    TObjectPtr<AAIAreaBase> AreaA = nullptr;

    /**
     * 비어 있으면 Exit 위치를 포함하는 Area를 C++에서 자동 판정합니다.
     * 특수한 맵 구조에서 자동 판정이 원하는 결과와 다를 때만 지정합니다.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Overrides", meta = (DisplayName = "Area B Override"))
    TObjectPtr<AAIAreaBase> AreaB = nullptr;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link")
    EAreaTraversalType TraversalType = EAreaTraversalType::Normal;

    /** Endpoint를 Actor 참조, 자동 계산, 수동 Point 중 어떤 방식으로 얻을지 결정합니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link")
    EAreaLinkEndpointMode EndpointMode = EAreaLinkEndpointMode::ActorReferences;

    /** false이면 AreaA에서 AreaB 방향으로만 사용할 수 있습니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link")
    bool bTwoWay = true;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link")
    bool bLinkEnabled = true;

    /**
     * 팀 이동 시스템이 실제 Teleport, JumpPad 등을 실행할 때 사용할 Actor입니다.
     * EntryActor가 비어 있으면 이 Actor의 위치를 진입점으로도 사용합니다.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Actors",
        meta = (DisplayName = "Traversal Actor A To B"))
    TObjectPtr<AActor> TraversalActor = nullptr;

    /**
     * Two Way 링크의 역방향(B -> A)에서 사용할 Goal Actor입니다.
     * 비어 있으면 Traversal Actor A To B를 재사용합니다.
     *
     * Jump/Drop처럼 방향마다 서로 다른 BP_WayPointLink를 사용하거나,
     * 양쪽에 각각 JumpPad/Teleport Actor가 있을 때 지정합니다.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Actors",
        meta = (DisplayName = "Traversal Actor B To A", EditCondition = "bTwoWay"))
    TObjectPtr<AActor> ReverseTraversalActor = nullptr;

    /**
     * 진입 위치 전용 Actor입니다. 비어 있으면 TraversalActor를 사용합니다.
     * 실제 실행 Actor의 원점과 AI가 도착해야 할 위치가 다를 때 지정합니다.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Actors")
    TObjectPtr<AActor> EntryActor = nullptr;

    /** Teleport 출구, JumpPad 착지 지점처럼 특수 이동 후 도착 위치를 나타내는 Actor입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Actors")
    TObjectPtr<AActor> ExitActor = nullptr;

    /**
     * Endpoint가 Area 경계에서 조금 벗어났을 때 가장 가까운 Area를 허용할 최대 거리입니다.
     * 0이면 반드시 Area 내부에 정확히 포함되어야 합니다.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Automatic", meta = (ClampMin = "0.0"))
    float AreaResolveTolerance = 150.0f;

    /** Automatic Drop에서 아래쪽 바닥을 찾는 최대 거리입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Automatic", meta = (ClampMin = "0.0"))
    float AutomaticDropTraceDistance = 3000.0f;

    /** Automatic Trace를 Endpoint보다 위에서 시작시키는 거리입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Automatic", meta = (ClampMin = "0.0"))
    float AutomaticTraceStartHeight = 100.0f;

    /** Automatic Jump에서 Link 또는 EntryActor의 전방으로 이동할 수평 거리입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Automatic", meta = (ClampMin = "0.0"))
    float AutomaticJumpForwardDistance = 500.0f;

    /** Automatic Jump 착지 후보에 추가할 높이 보정값입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Automatic")
    float AutomaticJumpHeightOffset = 0.0f;

    /** Automatic Jump 착지 후보 위쪽에서 Trace를 시작할 거리입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Automatic", meta = (ClampMin = "0.0"))
    float AutomaticJumpTraceUpDistance = 300.0f;

    /** Automatic Jump 착지 후보 아래쪽으로 Trace할 거리입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Automatic", meta = (ClampMin = "0.0"))
    float AutomaticJumpTraceDownDistance = 1000.0f;

    /** Automatic Door에서 문 중심을 기준으로 양쪽 Endpoint를 떨어뜨릴 거리입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Automatic", meta = (ClampMin = "0.0"))
    float AutomaticDoorHalfWidth = 100.0f;

    /** Drop/Jump 자동 착지점을 NavMesh 위로 보정할 때 사용할 검색 범위입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Automatic")
    FVector NavigationProjectionExtent = FVector(100.0f, 100.0f, 300.0f);

    /** Drop/Jump 자동 바닥 탐색에 사용할 Collision Channel입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Automatic")
    TEnumAsByte<ECollisionChannel> AutomaticTraceChannel = ECC_Visibility;

    /** 특수 이동 자체의 추가 거리 비용입니다. 순간이동은 보통 0으로 시작합니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Cost", meta = (ClampMin = "0.0"))
    float TraversalDistanceCost = 0.0f;

    /** 낙하 피해나 착지 노출처럼 Link 자체가 가지는 추가 위험도입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Link|Cost", meta = (ClampMin = "0.0"))
    float TraversalRiskCost = 0.0f;

private:
    AActor* GetEffectiveEntryActor() const;

    bool TryResolveActorReferenceEndpoints(
        FVector& OutEntryLocation,
        FVector& OutExitLocation,
        FString& OutFailureReason) const;

    bool TryResolveAutomaticEndpoints(
        UWorld* World,
        FVector& OutEntryLocation,
        FVector& OutExitLocation,
        FString& OutFailureReason) const;

    bool TryTraceAndProjectLandingPoint(
        UWorld* World,
        const FVector& TraceStart,
        const FVector& TraceEnd,
        FVector& OutLandingLocation,
        FString& OutFailureReason) const;

    bool TryProjectPointToNavigation(
        UWorld* World,
        const FVector& Point,
        FVector& OutProjectedPoint) const;

    void UpdateEndpointPreview();
};
