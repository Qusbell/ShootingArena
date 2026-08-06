#pragma once

#include "CoreMinimal.h"
#include "AreaTypes.generated.h"

class AAIAreaBase;
class AAreaLinkBase;
class AAIController;
class AActor;

/**
 * Area 사이를 이동할 때 사용하는 연결 종류입니다.
 * 실제 이동 실행은 팀 이동 시스템이 담당하고, Area 시스템은 경로 판단 정보만 제공합니다.
 */
UENUM(BlueprintType)
enum class EAreaTraversalType : uint8
{
    Normal      UMETA(DisplayName = "Normal"),
    Teleport    UMETA(DisplayName = "Teleport"),
    JumpPad     UMETA(DisplayName = "Jump Pad"),
    Drop        UMETA(DisplayName = "Drop"),
    Jump        UMETA(DisplayName = "Jump"),
    Door        UMETA(DisplayName = "Door")
};

/**
 * AreaLink가 진입점과 도착점을 결정하는 방식입니다.
 * Actor 참조, 자동 계산, 수동 보정 중 하나를 선택합니다.
 */
UENUM(BlueprintType)
enum class EAreaLinkEndpointMode : uint8
{
    /** TraversalActor 또는 EntryActor와 ExitActor의 위치를 사용합니다. */
    ActorReferences UMETA(DisplayName = "Actor References"),

    /** Link 종류별 C++ 자동 계산을 사용합니다. */
    Automatic UMETA(DisplayName = "Automatic"),

    /** 기존 AreaAPoint와 AreaBPoint를 직접 배치하는 수동 보정 방식입니다. */
    ManualOverride UMETA(DisplayName = "Manual Override")
};

/**
 * PPT의 Area_Manager 위험도 계산 항목을 그대로 확인하기 위한 결과입니다.
 * 설정값은 DA_Area_Risk의 세 점수를 사용하고, 동적 정보는 Observer AI별로 계산합니다.
 */
USTRUCT(BlueprintType)
struct SHOOTINGARENA_API FAreaRiskScoreResult
{
    GENERATED_BODY()

    /** 구조 위험도와 동적 위험도를 합산한 최종 위험도입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Risk", meta = (DisplayName = "Area Risk Score"))
    int32 AreaRiskScore = 0;

    /** 막다른 길 조건으로 계산한 구조 위험도입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Risk", meta = (DisplayName = "Structural Risk Score"))
    int32 StructuralRiskScore = 0;

    /** 유닛 수 점수와 교전 발생 점수를 합산한 동적 위험도입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Risk", meta = (DisplayName = "Dynamic Risk Score"))
    int32 DynamicRiskScore = 0;

    /** 연결된 구역 수가 1개 이하인지 나타냅니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Risk", meta = (DisplayName = "Is Dead End"))
    bool bIsDeadEnd = false;

    /** 현재 Area에서 이동 가능한 서로 다른 연결 구역 수입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Risk", meta = (DisplayName = "Connected Area Count"))
    int32 ConnectedAreaCount = 0;

    /**
     * 해당 AI의 RecognizedAreas 기억에 이 Area가 들어 있는지 나타냅니다.
     * false이면 구조 위험도는 계산되지만 유닛/교전 동적 위험도는 적용되지 않습니다.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Risk", meta = (DisplayName = "Is Area Recognized"))
    bool bIsAreaRecognized = false;

    /** 해당 AI가 이 Area에서 현재 인식 중인 생존 유닛 수입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Risk", meta = (DisplayName = "Recognized Unit Count"))
    int32 RecognizedUnitCount = 0;

    /** RecognizedUnitCount * UnitCountWeight 결과입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Risk", meta = (DisplayName = "Unit Count Score"))
    int32 UnitCountScore = 0;

    /** 해당 AI가 이 Area에서 교전 발생을 확인했는지 나타냅니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Risk", meta = (DisplayName = "Is Combat Detected"))
    bool bIsCombatDetected = false;

    /** 교전 발생 시 DA_Area_Risk에서 가져온 점수입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Risk", meta = (DisplayName = "Combat Score"))
    int32 CombatScore = 0;
};

/**
 * AI가 사용할 수 있는 특수 이동 종류입니다.
 * 사용할 수 없는 Link는 경로 후보에서 제외됩니다.
 */
USTRUCT(BlueprintType)
struct SHOOTINGARENA_API FAreaTraversalCapabilities
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Traversal")
    bool bCanUseNormal = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Traversal")
    bool bCanUseTeleport = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Traversal")
    bool bCanUseJumpPad = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Traversal")
    bool bCanUseDrop = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Traversal")
    bool bCanUseJump = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Traversal")
    bool bCanUseDoor = true;

    /** 특정 이동 종류를 사용할 수 있는지 반환합니다. */
    bool Supports(const EAreaTraversalType TraversalType) const
    {
        switch (TraversalType)
        {
        case EAreaTraversalType::Normal:   return bCanUseNormal;
        case EAreaTraversalType::Teleport: return bCanUseTeleport;
        case EAreaTraversalType::JumpPad:  return bCanUseJumpPad;
        case EAreaTraversalType::Drop:     return bCanUseDrop;
        case EAreaTraversalType::Jump:     return bCanUseJump;
        case EAreaTraversalType::Door:     return bCanUseDoor;
        default:                           return false;
        }
    }
};

/**
 * 맵에 저장되는 방향성 Area 연결 정보입니다.
 * 자동 Normal 연결은 SourceLink가 비어 있을 수 있습니다.
 */
USTRUCT(BlueprintType)
struct SHOOTINGARENA_API FAreaBakedConnection
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph")
    TObjectPtr<AAIAreaBase> FromArea = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph")
    TObjectPtr<AAIAreaBase> ToArea = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph")
    TObjectPtr<AAreaLinkBase> SourceLink = nullptr;

    /** 팀 이동 시스템에서 실제 특수 이동 Actor가 필요할 때 사용할 참조입니다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph")
    TObjectPtr<AActor> TraversalActor = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph")
    EAreaTraversalType TraversalType = EAreaTraversalType::Normal;

    /** FromArea 쪽에서 특수 이동을 시작하는 위치입니다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph")
    FVector EntryLocation = FVector::ZeroVector;

    /** 특수 이동 후 ToArea 쪽에 도착하는 위치입니다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph")
    FVector ExitLocation = FVector::ZeroVector;

    /** 순간이동, 점프 등 특수 이동 자체에 추가할 거리 비용입니다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph", meta = (ClampMin = "0.0"))
    float TraversalDistanceCost = 0.0f;

    /** Link 자체가 가지는 추가 위험도입니다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph", meta = (ClampMin = "0.0"))
    float TraversalRiskCost = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph")
    bool bEnabled = true;

    /** 수동 배치 Link가 아니라 자동으로 생성된 Normal 연결인지 표시합니다. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|Graph")
    bool bAutomatic = false;
};

/**
 * 에디터 Rebuild에서 계산해 맵에 저장하는 연결 간 Nav 이동 거리입니다.
 * PreviousConnectionIndex의 Exit에서 NextConnectionIndex의 보행 목표까지의 거리이며,
 * 런타임 Area 경로 비교에서는 Nav 조회 없이 이 값만 사용합니다.
 */
USTRUCT()
struct SHOOTINGARENA_API FAreaBakedTransitionDistance
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Area|Graph|Cache")
    int32 PreviousConnectionIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Area|Graph|Cache")
    int32 NextConnectionIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Area|Graph|Cache", meta = (ClampMin = "0.0"))
    float NavDistance = 0.0f;

    /** false이면 에디터 Bake 시점에 두 고정 지점 사이의 Nav 경로가 없었습니다. */
    UPROPERTY(VisibleAnywhere, Category = "Area|Graph|Cache")
    bool bReachable = false;
};

/**
 * 후퇴 후보마다 ProjectPointToNavigation을 반복하지 않도록 Area별로 저장하는 Nav 유효 지점입니다.
 * 한 Area에 여러 지점을 저장하고 런타임에는 현재 AI와 가장 가까운 지점만 선택합니다.
 */
USTRUCT()
struct SHOOTINGARENA_API FAreaBakedRetreatPoint
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Area|Graph|Cache")
    TObjectPtr<AAIAreaBase> Area = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Area|Graph|Cache")
    FVector Location = FVector::ZeroVector;
};

/** 최종 경로를 구성하는 한 단계입니다. */
USTRUCT(BlueprintType)
struct SHOOTINGARENA_API FAreaRouteStep
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    TObjectPtr<AAIAreaBase> FromArea = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    TObjectPtr<AAIAreaBase> ToArea = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    TObjectPtr<AAreaLinkBase> SourceLink = nullptr;

    /** 팀 이동 시스템에 전달할 수 있는 실제 Teleport, JumpPad 등의 Actor입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    TObjectPtr<AActor> TraversalActor = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    EAreaTraversalType TraversalType = EAreaTraversalType::Normal;

    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    FVector EntryLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    FVector ExitLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    float TraversalDistanceCost = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    float TraversalRiskCost = 0.0f;
};

/** 안전 경로 요청에 필요한 값입니다. */
USTRUCT(BlueprintType)
struct SHOOTINGARENA_API FAreaRouteRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Route")
    FVector StartPosition = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Route")
    FVector TargetPosition = FVector::ZeroVector;

    /** 동적 위험도를 어떤 AI의 인식 정보 기준으로 계산할지 지정합니다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Route")
    TObjectPtr<AAIController> ObserverController = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Route")
    FAreaTraversalCapabilities TraversalCapabilities;

    /** true이면 현재 AI가 이미 서 있는 시작 Area의 위험도도 합산합니다. 기본값은 false입니다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Route")
    bool bIncludeStartAreaRisk = false;

    /** 기존 호출 호환용 옵션입니다. 런타임 Area 비교는 Nav를 호출하지 않고 Bake 캐시 또는 직선거리 보정을 사용합니다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Route")
    bool bAllowStraightLineFallback = false;

    /** 위험도를 같은 값으로 판단할 때 사용할 허용 오차입니다. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Route", meta = (ClampMin = "0.0"))
    float RiskEqualityTolerance = 0.01f;
};

/** Area 경로 계산 결과입니다. */
USTRUCT(BlueprintType)
struct SHOOTINGARENA_API FAreaRouteResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    bool bSuccess = false;

    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    FText FailureReason;

    /** 시작 Area부터 목표 Area까지 정방향 순서로 저장됩니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    TArray<TObjectPtr<AAIAreaBase>> RouteAreas;

    /** RouteAreas 사이에서 사용할 방향성 Link 단계입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    TArray<FAreaRouteStep> RouteSteps;

    /** 요청한 AI의 인식 정보 기준으로 계산한 누적 위험도입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    float TotalRisk = 0.0f;

    /** Bake된 Nav 거리, 동적 시작/종료 구간의 추정 거리, 특수 이동 비용의 합입니다. */
    UPROPERTY(BlueprintReadOnly, Category = "Area|Route")
    float TotalTravelDistance = 0.0f;

    void Reset()
    {
        bSuccess = false;
        FailureReason = FText::GetEmpty();
        RouteAreas.Reset();
        RouteSteps.Reset();
        TotalRisk = 0.0f;
        TotalTravelDistance = 0.0f;
    }
};
