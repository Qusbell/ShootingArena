#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AreaRiskConfigDataAsset.generated.h"

/**
 * PPT에 정의된 위험도 점수와, 오래된 AI 기억을 자동으로 만료시키기 위한
 * 구현 보조 시간을 한 곳에서 관리하는 Primary Data Asset입니다.
 *
 * 점수 항목은 PPT 기준 세 가지만 사용합니다.
 * - Dead End Score
 * - Unit Count Weight
 * - Combat Occurrence Score
 *
 * Memory Lifetime은 새로운 위험도 종류가 아니라,
 * 마지막 인식 이후 오래된 정보가 영구히 남지 않게 하는 유효 시간입니다.
 */
UCLASS(BlueprintType)
class SHOOTINGARENA_API UAreaRiskConfigDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** 연결된 구역 수가 1개 이하인 막다른 길에 더하는 구조 위험도 점수입니다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Risk|Score", meta = (ClampMin = "0", DisplayName = "Dead End Score"))
    int32 DeadEndScore = 0;

    /** AI가 기억 중인 생존 유닛 1명당 더하는 위험도 점수입니다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Risk|Score", meta = (ClampMin = "0", DisplayName = "Unit Count Weight"))
    int32 UnitCountWeight = 0;

    /** AI가 해당 구역의 최근 교전을 기억하고 있을 때 더하는 위험도 점수입니다. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Risk|Score", meta = (ClampMin = "0", DisplayName = "Combat Occurrence Score"))
    int32 CombatOccurrenceScore = 0;

    /**
     * 유닛을 마지막으로 확인한 뒤 해당 유닛 기억을 유지하는 시간입니다.
     * 같은 유닛을 다시 확인하면 시간이 새로 갱신됩니다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Risk|Memory", meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s", DisplayName = "Unit Memory Lifetime"))
    float UnitMemoryLifetime = 5.0f;

    /**
     * 해당 구역에서 사격 또는 피격을 마지막으로 확인한 뒤
     * 교전 발생 기억을 유지하는 시간입니다. 다시 확인하면 시간이 갱신됩니다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area Risk|Memory", meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s", DisplayName = "Combat Memory Lifetime"))
    float CombatMemoryLifetime = 5.0f;
};
