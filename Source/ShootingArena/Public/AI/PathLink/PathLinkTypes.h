#pragma once

#include "CoreMinimal.h"
#include "PathLinkTypes.generated.h"

class APathLink;

/**
 * PathLink가 표현하는 특수 이동 종류입니다.
 * 색상은 APathLink 내부에서 타입별로 고정되며 Blueprint에서 변경할 수 없습니다.
 */
UENUM(BlueprintType)
enum class EPathLinkType : uint8
{
    Teleport UMETA(DisplayName = "Teleport"),
    JumpPad  UMETA(DisplayName = "JumpPad"),
    Jump     UMETA(DisplayName = "Jump"),
    Drop     UMETA(DisplayName = "Drop")
};

/** 최종 Route를 구성하는 한 구간의 종류입니다. */
UENUM(BlueprintType)
enum class EPathLinkSegmentType : uint8
{
    Normal UMETA(DisplayName = "Normal NavMesh"),
    Link   UMETA(DisplayName = "Path Link")
};

/**
 * 최단 경로를 구성하는 한 구간입니다.
 * Normal이면 PathPoints를 따라 NavMesh 이동을 하면 되고,
 * Link이면 Link의 실제 기믹 Entry까지 이동한 뒤 기존 기믹이 동작하도록 두면 됩니다.
 */
USTRUCT(BlueprintType)
struct SHOOTINGARENA_API FPathLinkRouteSegment
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    EPathLinkSegmentType SegmentType = EPathLinkSegmentType::Normal;

    /** 이 Segment가 시작되는 실제 월드 위치입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    FVector StartLocation = FVector::ZeroVector;

    /** 이 Segment가 끝나는 실제 월드 위치입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    FVector EndLocation = FVector::ZeroVector;

    /** 이 Segment의 순수 이동거리입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    double Distance = 0.0;

    /** Normal Segment일 때 NavMesh가 계산한 실제 Path Point입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    TArray<FVector> PathPoints;

    /** Link Segment일 때 사용한 PathLink입니다. Normal이면 nullptr입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    TObjectPtr<APathLink> Link = nullptr;

    /** TwoWay Link를 Exit -> Entry 방향으로 사용했다면 true입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    bool Reverse = false;

    /** Link Segment의 이동 종류입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    EPathLinkType LinkType = EPathLinkType::Teleport;
};

/** FindShortestRoute의 최종 반환값입니다. */
USTRUCT(BlueprintType)
struct SHOOTINGARENA_API FPathLinkRouteResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    bool Success = false;

    /** Normal NavMesh 거리 + Link 이동거리의 합입니다. Teleport 자체 거리는 0으로 계산합니다. */
    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    double TotalDistance = 0.0;

    /** 실제 실행 순서대로 정렬된 Route Segment입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    TArray<FPathLinkRouteSegment> Segments;

    /** 최종 Route에서 사용된 Link만 순서대로 담습니다. */
    UPROPERTY(BlueprintReadOnly, Category = "AI|PathLink|Route")
    TArray<TObjectPtr<APathLink>> UsedLinks;

    void Reset()
    {
        Success = false;
        TotalDistance = 0.0;
        Segments.Reset();
        UsedLinks.Reset();
    }
};

/**
 * Route Cache가 사용하는 한 방향의 PathLink 이동 정보입니다.
 * OneWay Link는 1개, TwoWay Link는 Forward/Reverse 2개의 Traversal로 변환됩니다.
 * Blueprint에 노출하지 않는 순수 C++ 내부 데이터입니다.
 */
struct SHOOTINGARENA_API FPathLinkTraversalNode
{
    TWeakObjectPtr<APathLink> Link;
    bool Reverse = false;
    EPathLinkType LinkType = EPathLinkType::Teleport;

    FVector EntryLocation = FVector::ZeroVector;
    FVector ExitLocation = FVector::ZeroVector;
    FVector EntryNavLocation = FVector::ZeroVector;
    FVector ExitNavLocation = FVector::ZeroVector;

    /** Link 자체를 통과하는 비용입니다. Teleport는 0, 나머지는 Entry-Exit 직선거리입니다. */
    double LinkCost = 0.0;
};

/**
 * 한 Traversal의 Exit에서 다른 Traversal의 Entry까지의 정적 NavMesh 이동 결과입니다.
 * 후보 전체의 PathPoints는 저장하지 않고 도달 가능 여부와 거리만 보관합니다.
 */
struct SHOOTINGARENA_API FPathLinkCachedTransition
{
    bool Reachable = false;
    double NavDistance = 0.0;
};

/**
 * PathLink들 사이의 정적 연결을 미리 계산한 Route Graph입니다.
 * PathfindingContext가 다르면 Navigation 결과가 달라질 수 있으므로 Subsystem이 Context별로 별도 Cache를 보관합니다.
 */
struct SHOOTINGARENA_API FPathLinkStaticGraph
{
    TArray<FPathLinkTraversalNode> Traversals;

    /** TraversalCount x TraversalCount 크기의 1차원 배열입니다. [From * Count + To] */
    TArray<FPathLinkCachedTransition> Transitions;

    int32 PrecomputedNavQueryCount = 0;

    void Reset()
    {
        Traversals.Reset();
        Transitions.Reset();
        PrecomputedNavQueryCount = 0;
    }

    int32 GetTraversalCount() const
    {
        return Traversals.Num();
    }

    const FPathLinkCachedTransition* GetTransition(const int32 From, const int32 To) const
    {
        const int32 Count = Traversals.Num();
        if (From < 0 || To < 0 || From >= Count || To >= Count)
        {
            return nullptr;
        }

        const int32 Index = From * Count + To;
        return Transitions.IsValidIndex(Index) ? &Transitions[Index] : nullptr;
    }
};
