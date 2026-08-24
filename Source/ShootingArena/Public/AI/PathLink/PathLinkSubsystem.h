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

    /** 현재 Enabled + 구조 Validation을 통과한 Link를 반환합니다. Client에서도 정상 배치 Link를 동일하게 조회할 수 있습니다. */
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

    /**
     * FindShortestRoute가 선택한 최종 Route의 Segments를 월드에 Debug Line으로 표시합니다.
     * OutResult를 Split한 상태에서도 Segments 핀을 바로 연결할 수 있습니다.
     * Normal Segment는 실제 NavMesh PathPoints를 따라 그리고, Link Segment는 실제 Entry -> Exit를 타입별 고정 색상으로 표시합니다.
     * Debug Draw는 네트워크로 복제되지 않으므로 이 함수를 호출한 World의 Viewport에서만 보입니다.
     */
    UFUNCTION(
        BlueprintCallable,
        Category = "AI|PathLink|Debug",
        meta = (
            BlueprintPure = "false",
            AdvancedDisplay = "Duration,Thickness,PersistentLines"
            )
    )
    void DrawDebugRoute(
        const TArray<FPathLinkRouteSegment>& RouteSegments,
        float Duration = 5.0f,
        float Thickness = 5.0f,
        bool PersistentLines = false);

    /** APathLink의 BeginPlay에서 자동 호출합니다. 외부 BP에서 직접 등록할 필요가 없습니다. */
    void RegisterLink(APathLink* Link);

    /** APathLink의 EndPlay에서 자동 호출합니다. */
    void UnregisterLink(APathLink* Link);

private:
    /**
     * 실제 서버/Standalone Route 계산에 사용할 수 있는 Link만 반환합니다.
     * Enabled + 구조 Validation + 현재 NavMesh Endpoint Projection을 모두 통과해야 합니다.
     * Client World에서는 빈 배열을 반환합니다.
     */
    TArray<APathLink*> GetUsableLinksForNavigation() const;

    /**
     * 중복 배치된 PathLink가 하나라도 있는지 검사합니다.
     * 중복은 단순 Invalid와 달리 전체 PathLink Route 실행을 막는 Blocking Error로 취급합니다.
     */
    bool HasBlockingDuplicateLinks(TArray<APathLink*>& OutDuplicateLinks, FString& OutSummary) const;

#if WITH_EDITOR
    /** PIE/SIE에서 중복 Link를 발견했을 때 팝업을 띄우고 실행을 즉시 종료합니다. */
    void BlockEditorPlayForDuplicates(const FString& Summary) const;
#endif

    /** WeakPtr로 보관해 Level Streaming / World Partition Unload 시 Actor 수명을 붙잡지 않습니다. */
    TArray<TWeakObjectPtr<APathLink>> RegisteredLinks;
};
