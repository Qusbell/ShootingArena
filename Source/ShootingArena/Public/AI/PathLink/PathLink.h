#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/PathLink/PathLinkTypes.h"
#include "PathLink.generated.h"

class USceneComponent;
class UBillboardComponent;

/**
 * Area 시스템과 독립된 순수 길찾기용 Link Actor입니다.
 * PathLink Actor 자신의 위치를 Entry로 사용하고 ExitActor만 지정해 두 지점을 연결합니다.
 */
UCLASS(Blueprintable)
class SHOOTINGARENA_API APathLink : public AActor
{
    GENERATED_BODY()

public:
    APathLink();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
    /** 레벨 뷰포트에서 ExitActor가 움직여도 Visual 선이 즉시 따라가도록 Editor World에서만 Tick합니다. */
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#endif

    /**
     * PathLink를 현재 XY 위치에서 아래 방향으로 Trace해 가장 먼저 찾은 지면 위치로 이동시킵니다.
     * 레벨 배치 편의를 위한 Editor 버튼이며, Actor 계층 Attach가 아니라 위치 Snap 기능입니다.
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "AI|PathLink|Placement", meta = (DisplayName = "Attach To Ground"))
    void AttachToGround();

    /**
     * Link의 배치/데이터 구조가 유효한지 검사합니다.
     * NavMesh/NavigationSystem 상태는 현재 World 역할에 따라 달라질 수 있으므로 여기서는 검사하지 않습니다.
     * Enabled는 검사하지 않습니다.
     */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink|Validation")
    bool IsValidLink() const;

    /** Enabled + 구조 Validation 기준으로 Link가 활성 상태인지 반환합니다. Navigation 사용 가능 여부는 별도로 검사합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink|Validation")
    bool IsUsable() const { return Enabled && IsValidLink(); }

    /**
     * IsValidLink와 동일한 검사를 수행하고, 실패한 모든 이유를 한 번에 반환합니다.
     * 여러 오류가 있으면 줄바꿈으로 구분됩니다.
     */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink|Validation")
    bool ValidateLink(FText& OutFailureReason) const;

    /**
     * ValidateLink를 수행하고 결과를 Output Log에 출력합니다.
     * Editor에서 PathLink를 선택한 뒤 버튼으로도 실행할 수 있습니다.
     */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink|Validation")
    bool ValidateAndLog() const;

    /** 선택한 PathLink를 Editor Details 버튼으로 검사하고 결과를 Output Log에 출력합니다. */
    UFUNCTION(CallInEditor, Category = "AI|PathLink|Validation", meta = (DisplayName = "Validate Path Link"))
    void ValidateInEditor();

    /** 정방향 Entry -> Exit의 실제 진입 위치입니다. PathLink Actor 자신의 월드 위치를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    FVector GetEntryLocation() const;

    /** 정방향 Entry -> Exit의 실제 출구 위치를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    FVector GetExitLocation() const;

    /**
     * 실제 이동 방향을 기준으로 진입/출구 위치를 반환합니다.
     * Reverse=false : PathLink(Self) -> ExitActor
     * Reverse=true  : ExitActor -> PathLink(Self) (TwoWay가 true여야 함)
     */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink")
    bool ResolveTravelLocations(
        bool Reverse,
        FVector& OutEntryLocation,
        FVector& OutExitLocation,
        FText& OutFailureReason) const;

    /** Reverse 방향까지 반영한 Link 자체의 순수 이동거리를 반환합니다. Teleport는 0입니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    double GetTravelDistance(bool Reverse) const;

    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    EPathLinkType GetLinkType() const { return LinkType; }

    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    AActor* GetExitActor() const { return ExitActor; }

    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    bool IsTwoWay() const { return TwoWay; }

    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    bool IsEnabled() const { return Enabled; }

    /** C++ Route Finder에서 사용하는 Endpoint 계산 함수입니다. */
    bool TryResolveTravelLocations(
        bool Reverse,
        FVector& OutEntryLocation,
        FVector& OutExitLocation,
        FString& OutFailureReason) const;

    /**
     * 서버/Standalone의 현재 NavigationSystem 기준으로 Entry/Exit을 실제 Route에 사용할 수 있는지 검사합니다.
     * Blueprint 배치 Validation과 분리된 런타임 Navigation 검사이며 Client World에서는 false를 반환합니다.
     */
    bool CanUseForNavigation() const;

    /** Subsystem 자동 등록 시 사용합니다. 구조 Invalid일 때만 상세 오류를 Output Log에 출력합니다. */
    bool LogValidationErrors() const;

    /**
     * 현재 Link가 다른 PathLink와 중복 배치되어 있는지 검사합니다.
     * PIE 실행 차단 및 Route 계산 차단용 내부 가드레일에서 사용합니다.
     */
    bool HasDuplicatePlacementError(FString& OutDetails) const;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|PathLink|Components")
    TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
    /**
     * 레벨 뷰포트에서 PathLink를 직접 클릭/선택하기 위한 Editor 전용 아이콘입니다.
     * 실제 Entry 위치는 PathLink Actor 자신의 위치이며, 이 아이콘은 선택 편의만 담당합니다.
     */
    UPROPERTY(VisibleAnywhere, Category = "AI|PathLink|Components")
    TObjectPtr<UBillboardComponent> EditorIcon;
#endif

    /** 특수 이동 종류입니다. Visual 색상도 이 값으로 자동 결정됩니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink")
    EPathLinkType LinkType = EPathLinkType::Teleport;

    /**
     * 특수 이동이 끝난 뒤 나오는 위치를 나타내는 Actor입니다.
     * Entry는 별도 Actor를 지정하지 않고 PathLink 자신의 위치를 사용합니다.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink")
    TObjectPtr<AActor> ExitActor = nullptr;

    /** false: Self -> Exit, true: Self -> Exit와 Exit -> Self를 모두 허용합니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink")
    bool TwoWay = false;

    /** false이면 Subsystem에는 존재하지만 실제 최단 경로 후보에서는 제외됩니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink")
    bool Enabled = true;

    /** 에디터 뷰포트의 연결선/화살표 표시 여부입니다. 실제 길찾기에는 영향을 주지 않습니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink|Visual")
    bool ShowVisual = true;

    /** ExitActor 기준 Local Space 위치 보정입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink|Advanced")
    FVector ExitOffset = FVector::ZeroVector;

private:
    /** 타입에 맞는 기존 BP Component를 찾아 Exit 위치를 계산합니다. 못 찾으면 ActorLocation을 사용합니다. */
    FVector ResolveExitPoint(AActor* Actor, const FVector& LocalOffset) const;

    /** 배치/데이터 구조 Validation 오류를 "[Part] 상세 이유" 형식으로 수집합니다. Navigation 상태는 포함하지 않습니다. */
    void CollectValidationErrors(TArray<FString>& OutErrors) const;

    /** 현재 서버/Standalone World의 Navigation 사용 가능 여부를 별도로 수집합니다. */
    void CollectNavigationErrors(TArray<FString>& OutErrors) const;

    /** 지정 위치가 현재 World의 NavMesh에 투영 가능한지 검사합니다. */
    bool CanProjectToNavigation(const FVector& WorldLocation) const;

    /** 다른 PathLink와 같은 연결을 중복으로 만들고 있는지 검사합니다. */
    void CollectDuplicatePlacementErrors(
        const FVector& ResolvedEntry,
        const FVector& ResolvedExit,
        TArray<FString>& OutErrors) const;

    /** Attach To Ground에서 사용할 아래 방향 지면 Trace입니다. */
    bool TraceGround(FHitResult& OutHit) const;

#if WITH_EDITOR
    /** 타입별 고정 Visual 색상입니다. Blueprint/Details에서 변경할 수 없습니다. */
    FColor GetVisualColor() const;
    void DrawEditorVisual() const;
    void DrawArrowHead(const FVector& Tip, const FVector& Direction, const FColor& Color) const;
#endif
};
