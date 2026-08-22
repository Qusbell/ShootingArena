#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/PathLink/PathLinkTypes.h"
#include "PathLink.generated.h"

class USceneComponent;

/**
 * Area 시스템과 독립된 순수 길찾기용 Link Actor입니다.
 * 기존 Teleport / JumpPad BP를 수정하지 않고, 해당 Actor를 Entry / Exit로 참조합니다.
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
    /** 레벨 뷰포트에서 참조 Actor가 움직여도 Visual 선이 즉시 따라가도록 Editor World에서만 Tick합니다. */
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#endif

    /**
     * Link가 실제 길찾기에 들어갈 수 있는 구조인지 꼼꼼하게 검사합니다.
     * Enabled는 검사하지 않습니다. Enabled까지 포함한 사용 가능 여부는 IsUsable을 사용합니다.
     */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink|Validation")
    bool IsValidLink() const;

    /** Enabled까지 포함해 현재 길찾기에서 사용할 수 있는 Link인지 반환합니다. */
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
     * 오류는 [PathLink][INVALID] 형식으로 Link 이름 / Type / 문제 Part / 상세 이유를 각각 출력합니다.
     */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink|Validation")
    bool ValidateAndLog() const;

    /** 정방향 Entry -> Exit의 실제 진입 위치를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    FVector GetEntryLocation() const;

    /** 정방향 Entry -> Exit의 실제 출구 위치를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    FVector GetExitLocation() const;

    /**
     * 실제 이동 방향을 기준으로 진입/출구 위치를 반환합니다.
     * Reverse=false : EntryActor -> ExitActor
     * Reverse=true  : ExitActor -> EntryActor (TwoWay가 true여야 함)
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
    AActor* GetEntryActor() const { return EntryActor; }

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

    /** Subsystem 자동 등록 시 사용합니다. Invalid일 때만 상세 오류를 Output Log에 출력합니다. */
    bool LogValidationErrors() const;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|PathLink|Components")
    TObjectPtr<USceneComponent> SceneRoot;

    /** 특수 이동 종류입니다. Visual 색상도 이 값으로 자동 결정됩니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink")
    EPathLinkType LinkType = EPathLinkType::Teleport;

    /** AI가 특수 이동을 사용하기 위해 진입해야 하는 Actor입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink")
    TObjectPtr<AActor> EntryActor = nullptr;

    /** 특수 이동이 끝난 뒤 나오는 위치를 나타내는 Actor입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink")
    TObjectPtr<AActor> ExitActor = nullptr;

    /** false: Entry -> Exit, true: Entry -> Exit와 Exit -> Entry를 모두 허용합니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink")
    bool TwoWay = false;

    /** false이면 Subsystem에는 존재하지만 실제 최단 경로 후보에서는 제외됩니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink")
    bool Enabled = true;

    /** 에디터 뷰포트의 연결선/화살표 표시 여부입니다. 실제 길찾기에는 영향을 주지 않습니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink|Visual")
    bool ShowVisual = true;

    /** EntryActor 기준 Local Space 위치 보정입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink|Advanced")
    FVector EntryOffset = FVector::ZeroVector;

    /** ExitActor 기준 Local Space 위치 보정입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|PathLink|Advanced")
    FVector ExitOffset = FVector::ZeroVector;

private:
    /** 타입에 맞는 기존 BP Component를 찾아 실제 사용 위치를 계산합니다. 못 찾으면 ActorLocation을 사용합니다. */
    FVector ResolveEntryPoint(AActor* Actor, const FVector& LocalOffset) const;
    FVector ResolveExitPoint(AActor* Actor, const FVector& LocalOffset) const;

    /** 모든 Validation 오류를 "[Part] 상세 이유" 형식으로 수집합니다. */
    void CollectValidationErrors(TArray<FString>& OutErrors) const;

    /** 지정 위치가 현재 World의 NavMesh에 투영 가능한지 검사합니다. */
    bool CanProjectToNavigation(const FVector& WorldLocation) const;

#if WITH_EDITOR
    /** 타입별 고정 Visual 색상입니다. Blueprint/Details에서 변경할 수 없습니다. */
    FColor GetVisualColor() const;
    void DrawEditorVisual() const;
    void DrawArrowHead(const FVector& Tip, const FVector& Direction, const FColor& Color) const;
#endif
};
