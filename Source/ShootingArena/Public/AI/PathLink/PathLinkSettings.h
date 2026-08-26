#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PathLinkSettings.generated.h"

/**
 * PathLink Route의 런타임 후보 축소/보정 값을 Project Settings에서 조정하기 위한 설정입니다.
 * Link -> Link는 정밀한 NavMesh Graph를 그대로 사용하고,
 * Start -> 첫 Link / 마지막 Link -> Target은 Nearest 후보의 직선거리 + LineTrace 보정으로 먼저 비교하고, 실제로 선택된 Endpoint만 NavMesh로 정밀 검증하여 Query Spike를 줄입니다.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Path Link"))
class SHOOTINGARENA_API UPathLinkSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    /** Start/Target 각각에서 우선 검사할 가까운 PathLink 수입니다. TwoWay도 같은 Link는 한 후보로만 계산합니다. */
    UPROPERTY(Config, EditAnywhere, Category = "Route Optimization", meta = (ClampMin = "1", UIMin = "1", UIMax = "32"))
    int32 DynamicCandidateCount = 6;

    /** 후보 선별용 Line Trace가 벽/장애물에 막혔을 때 직선거리에 곱하는 보정값입니다. */
    UPROPERTY(Config, EditAnywhere, Category = "Route Optimization", meta = (ClampMin = "1.0", UIMin = "1.0", UIMax = "4.0"))
    float BlockedTraceDistanceMultiplier = 1.5f;

    /** 바닥에 Line Trace가 바로 맞는 문제를 줄이기 위해 후보 Trace의 시작/끝을 Z축으로 띄우는 높이(cm)입니다. */
    UPROPERTY(Config, EditAnywhere, Category = "Route Optimization", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "250.0"))
    float CandidateTraceHeight = 100.0f;

    /** 선택된 Endpoint의 실제 NavPath 거리가 직선거리+Trace 예상치보다 이 배수 이상 길면 후보 범위를 추가 확장합니다. */
    UPROPERTY(Config, EditAnywhere, Category = "Route Optimization", meta = (ClampMin = "1.0", UIMin = "1.0", UIMax = "10.0"))
    float ExcessiveNavDistanceRatio = 3.5f;

    /** 안전장치가 발동했을 때 Start/Target 쪽에서 추가로 검사할 PathLink 수입니다. */
    UPROPERTY(Config, EditAnywhere, Category = "Route Optimization", meta = (ClampMin = "1", UIMin = "1", UIMax = "16"))
    int32 CandidateExpansionStep = 3;

    /** 후보 확장을 반복할 최대 횟수입니다. 지나친 NavMesh Query Spike를 막기 위한 상한입니다. */
    UPROPERTY(Config, EditAnywhere, Category = "Route Optimization", meta = (ClampMin = "0", UIMin = "0", UIMax = "8"))
    int32 MaxCandidateExpansionPasses = 3;
};
