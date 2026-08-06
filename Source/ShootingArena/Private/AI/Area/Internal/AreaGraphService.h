#pragma once

#include "CoreMinimal.h"
#include "AI/Area/AreaTypes.h"

class AAIAreaBase;
class AAreaLinkBase;
class AAreaManagerActorBase;
class UWorld;

/** 런타임에서 사용하는 가벼운 방향성 연결입니다. */
struct FAreaDirectedConnection
{
    TWeakObjectPtr<AAIAreaBase> FromArea;
    TWeakObjectPtr<AAIAreaBase> ToArea;
    TWeakObjectPtr<AAreaLinkBase> SourceLink;
    TWeakObjectPtr<AActor> TraversalActor;
    EAreaTraversalType TraversalType = EAreaTraversalType::Normal;
    FVector EntryLocation = FVector::ZeroVector;
    FVector ExitLocation = FVector::ZeroVector;
    float TraversalDistanceCost = 0.0f;
    float TraversalRiskCost = 0.0f;
    bool bEnabled = true;
    bool bAutomatic = false;
    int32 BakedConnectionIndex = INDEX_NONE;
};

/**
 * Area 수집, 연결 생성, 위치 기반 Area 검색을 담당하는 일반 C++ Service입니다.
 * UObject가 아니므로 GC 대상 Actor는 Weak Pointer로 보관합니다.
 */
class FAreaGraphService
{
public:
    bool BuildFromWorldActors(UWorld* World, const AAreaManagerActorBase* ManagerActor);
    bool LoadBakedConnections(UWorld* World, const AAreaManagerActorBase* ManagerActor);

    void ExportBakedConnections(TArray<FAreaBakedConnection>& OutConnections) const;

    /** 에디터 Rebuild 전용: 고정 연결 사이의 Nav 거리와 Area별 후퇴 지점을 계산합니다. */
    void BuildEditorNavigationCaches(
        UWorld* World,
        float RetreatPointInset,
        TArray<FAreaBakedTransitionDistance>& OutTransitionDistances,
        TArray<FAreaBakedRetreatPoint>& OutRetreatPoints) const;

    /** 런타임에는 저장된 캐시만 읽고 NavigationSystem을 호출하지 않습니다. */
    bool TryGetBakedTransitionDistance(
        int32 PreviousConnectionIndex,
        int32 NextConnectionIndex,
        float& OutDistance,
        bool& OutReachable) const;

    bool GetBestBakedRetreatPoint(
        const AAIAreaBase* Area,
        const FVector& FromPosition,
        FVector& OutPoint) const;

    bool HasBakedRuntimeCaches() const { return bHasBakedRuntimeCaches; }
    void Reset();

    AAIAreaBase* FindAreaByPosition(const FVector& WorldPosition) const;

    const TArray<TWeakObjectPtr<AAIAreaBase>>& GetAreas() const { return Areas; }
    const TArray<FAreaDirectedConnection>& GetConnections() const { return Connections; }
    const TArray<int32>& GetOutgoingConnectionIndices(const AAIAreaBase* Area) const;

    void Validate(TArray<FString>& OutIssues) const;

private:
    void CollectAreas(UWorld* World);
    void AddManualLinks(UWorld* World, TSet<uint32>& OutManualPairHashes);
    void AddAutomaticNormalLinks(UWorld* World, const AAreaManagerActorBase* ManagerActor, const TSet<uint32>& ManualPairHashes);
    void AddDirectedConnection(const FAreaDirectedConnection& Connection);
    void RebuildAdjacency();

    /** Endpoint가 Area 경계에서 조금 벗어났을 때 지정 거리 안의 가장 가까운 Area를 찾습니다. */
    AAIAreaBase* FindAreaByPositionOrNearest(const FVector& WorldPosition, float MaxDistance) const;

    static uint32 MakeDirectedPairHash(const AAIAreaBase* FromArea, const AAIAreaBase* ToArea);
    static float ComputeBoundsGapXY(const FBox& A, const FBox& B);

    /**
     * Area 전체 XY 둘레에서 일정 간격으로 후보 지점을 생성합니다.
     * 문이나 통로가 Area 중심선 또는 마주 보는 한쪽 면에 없더라도 찾을 수 있습니다.
     */
    static void BuildBoundaryCandidates(
        const AAIAreaBase* SourceArea,
        const AAIAreaBase* TargetArea,
        float Inset,
        float SampleSpacing,
        TArray<FVector>& OutCandidates);

    /** 후보 지점을 NavMesh 위로 투영하고 Area 근처에 있는 지점만 남깁니다. */
    static bool TryProjectCandidateToNavigation(
        UWorld* World,
        const AAIAreaBase* OwningArea,
        const FVector& Candidate,
        FVector& OutProjectedPoint);

    /**
     * 두 Area 경계 후보 조합을 모두 검사해 실제 NavMesh 경로가 존재하는
     * 가장 짧은 Entry/Exit 지점 쌍을 선택합니다.
     */
    static bool TryFindBestAutomaticNormalEndpoints(
        UWorld* World,
        const AAIAreaBase* AreaA,
        const AAIAreaBase* AreaB,
        float Inset,
        float SampleSpacing,
        float MaxHeightDifference,
        float MaxPathDetourRatio,
        float MaxPathExtraDistance,
        FVector& OutPointA,
        FVector& OutPointB,
        double& OutPathLength,
        FString& OutFailureReason);

    static bool TryGetNavigationPathLength(
        UWorld* World,
        const FVector& Start,
        const FVector& End,
        double& OutLength);

    static uint64 MakeTransitionCacheKey(int32 PreviousConnectionIndex, int32 NextConnectionIndex);

    TArray<TWeakObjectPtr<AAIAreaBase>> Areas;
    TArray<FAreaDirectedConnection> Connections;
    TMap<const AAIAreaBase*, TArray<int32>> OutgoingConnections;
    TArray<int32> EmptyConnectionIndices;

    /** Rebuild 중 해석하지 못한 수동 Link의 상세 원인을 Validate에 전달합니다. */
    TArray<FString> BuildIssues;

    TMap<uint64, FAreaBakedTransitionDistance> TransitionDistanceCache;
    TMap<const AAIAreaBase*, TArray<FVector>> RetreatPointsByArea;
    bool bHasBakedRuntimeCaches = false;
};
