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
