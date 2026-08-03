#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BTService.h"
#include "AI/Area/AreaTypes.h"
#include "BTS_UpdateRetreatDecision.generated.h"

/**
 * 현재 Area보다 안전하고 실제로 도달 가능한 경로가 있는지 주기적으로 판단하여
 * needRetreat Blackboard Key를 자동으로 갱신합니다.
 *
 * 이 Service는 후퇴 필요 여부만 결정합니다.
 * 목표 Actor 준비와 실제 이동은 Select Retreat Area Step과 팀 이동 Task가 담당합니다.
 */
UCLASS(meta = (DisplayName = "Update Retreat Decision"))
class SHOOTINGARENA_API UBTS_UpdateRetreatDecision : public UBTService
{
    GENERATED_BODY()

public:
    UBTS_UpdateRetreatDecision();

protected:
    virtual void OnBecomeRelevant(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;

    virtual void TickNode(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory,
        float DeltaSeconds) override;

    /** 자동으로 갱신할 후퇴 필요 여부 Bool Key입니다. */
    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector needRetreatKey;

    /** 후퇴 이동 중인지 저장하는 Bool Key입니다. */
    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector isRetreatingKey;

    /** 현재 이동 시스템이 실제로 실행할 수 있는 이동 종류만 켭니다. */
    UPROPERTY(EditAnywhere, Category = "Route Settings")
    FAreaTraversalCapabilities traversalCapabilities;

    /** 현재 Area보다 최소한 이 값만큼 안전한 Area가 있어야 후퇴를 시작합니다. */
    UPROPERTY(EditAnywhere, Category = "Route Settings", meta = (ClampMin = "0.0"))
    float minimumRiskImprovement = 0.01f;

    /** 두 경로의 누적 위험도를 같은 값으로 판단할 허용 오차입니다. */
    UPROPERTY(EditAnywhere, Category = "Route Settings", meta = (ClampMin = "0.0"))
    float riskEqualityTolerance = 0.01f;

    /** 후보 Area 내부 지점을 NavMesh에 투영할 때 사용하는 탐색 범위입니다. */
    UPROPERTY(EditAnywhere, Category = "Route Settings", meta = (ClampMin = "0.0"))
    FVector navProjectionExtent = FVector(300.0, 300.0, 500.0);

    /** 후보 Area 경계에서 안쪽으로 밀어 넣는 거리입니다. */
    UPROPERTY(EditAnywhere, Category = "Route Settings", meta = (ClampMin = "0.0"))
    float candidateInset = 80.0f;

private:
    void UpdateRetreatDecision(UBehaviorTreeComponent& OwnerComp) const;
};
