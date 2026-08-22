#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AI/PathLink/PathLinkTypes.h"
#include "PathLinkSubsystem.generated.h"

class AActor;
class APathLink;

/**
 * 현재 World에 존재하는 PathLink를 자동 관리하고,
 * Blueprint / AI 로직이 길찾기 기능을 호출하는 단일 진입점입니다.
 */
UCLASS()
class SHOOTINGARENA_API UPathLinkSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    /**
     * 현재 World의 APathLink를 다시 검색해 Registry를 재구성합니다.
     * 평소에는 Link의 BeginPlay/EndPlay 자동 등록을 사용하고, 강제 갱신/디버그가 필요할 때만 호출하면 됩니다.
     */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink")
    void RefreshLinks();

    /** 현재 World에서 등록된 모든 Link를 반환합니다. Disabled/Invalid Link도 포함합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    TArray<APathLink*> GetAllLinks() const;

    /** 현재 실제 길찾기에서 사용 가능한 Enabled + Valid Link만 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    TArray<APathLink*> GetEnabledLinks() const;

    /** 현재 Registry에서 Validation에 실패한 Link만 반환합니다. Enabled 여부와는 무관합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink|Validation")
    TArray<APathLink*> GetInvalidLinks() const;

    /**
     * 현재 Registry의 모든 Link를 다시 검사하고 Invalid 상세 내용을 Output Log에 출력합니다.
     * 레벨 배치 검수나 디버깅 시 BP에서 수동으로 호출할 수 있습니다.
     */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink|Validation")
    bool ValidateAllLinks(int32& OutValidCount, int32& OutInvalidCount) const;

    /** 지정한 Type의 등록된 Link를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    TArray<APathLink*> GetLinksByType(EPathLinkType LinkType, bool OnlyEnabled = true) const;

    /** 현재 Registry에 들어 있는 Link 개수입니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    int32 GetLinkCount() const;

    /** 위치에서 가장 가까운 Link를 반환합니다. Entry/Exit 중 가까운 쪽을 기준으로 계산합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|PathLink")
    APathLink* GetNearestLink(const FVector& Location, bool OnlyEnabled = true) const;

    /** World 위치를 가장 가까운 NavMesh 위치로 보정합니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink|Navigation")
    bool ProjectToNavigation(const FVector& WorldLocation, FVector& OutNavLocation) const;

    /** Link를 사용하지 않고 NavMesh만으로 두 위치 사이의 실제 이동거리를 반환합니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink|Navigation", meta = (AdvancedDisplay = "PathfindingContext"))
    bool GetNavPathDistance(
        const FVector& StartLocation,
        const FVector& TargetLocation,
        double& OutDistance,
        AActor* PathfindingContext = nullptr) const;

    /**
     * 메인 함수입니다.
     * NavMesh 일반 이동 + 현재 Enabled PathLink를 모두 비교해 순수 이동거리가 가장 짧은 Route를 반환합니다.
     */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink|Route", meta = (AdvancedDisplay = "PathfindingContext"))
    bool FindShortestRoute(
        const FVector& StartLocation,
        const FVector& TargetLocation,
        FPathLinkRouteResult& OutResult,
        AActor* PathfindingContext = nullptr) const;

    /** Actor를 Target으로 사용하는 FindShortestRoute 편의 함수입니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink|Route", meta = (AdvancedDisplay = "PathfindingContext"))
    bool FindShortestRouteToActor(
        const FVector& StartLocation,
        AActor* TargetActor,
        FPathLinkRouteResult& OutResult,
        AActor* PathfindingContext = nullptr) const;

    /** 최단 Route 전체 거리만 필요할 때 사용하는 편의 함수입니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink|Route", meta = (AdvancedDisplay = "PathfindingContext"))
    bool GetRouteDistance(
        const FVector& StartLocation,
        const FVector& TargetLocation,
        double& OutDistance,
        AActor* PathfindingContext = nullptr) const;

    /** NavMesh + Link를 사용했을 때 목표 위치까지 도달 가능한지만 검사합니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|PathLink|Route", meta = (AdvancedDisplay = "PathfindingContext"))
    bool CanReach(
        const FVector& StartLocation,
        const FVector& TargetLocation,
        AActor* PathfindingContext = nullptr) const;

    /** APathLink의 BeginPlay에서 자동 호출합니다. 외부 BP에서 직접 등록할 필요가 없습니다. */
    void RegisterLink(APathLink* Link);

    /** APathLink의 EndPlay에서 자동 호출합니다. */
    void UnregisterLink(APathLink* Link);

private:
    /** WeakPtr로 보관해 Level Streaming / World Partition Unload 시 Actor 수명을 붙잡지 않습니다. */
    TArray<TWeakObjectPtr<APathLink>> RegisteredLinks;
};
