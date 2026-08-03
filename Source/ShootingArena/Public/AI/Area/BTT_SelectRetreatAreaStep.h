#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "AI/Area/AreaTypes.h"
#include "BTT_SelectRetreatAreaStep.generated.h"

class AAIController;
class AActor;
class UBlackboardComponent;
class UWorld;

/**
 * 자동 후퇴 판단 Service가 needRetreat를 켠 뒤 실행됩니다.
 * 가장 안전한 전체 경로의 첫 번째 Step을 Blackboard에 기록합니다.
 *
 * 실제 이동은 실행하지 않으며, 팀장이 만든 Actor 기반 이동 Task가 담당합니다.
 */
UCLASS(meta = (DisplayName = "Select Retreat Area Step"))
class SHOOTINGARENA_API UBTT_SelectRetreatAreaStep : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTT_SelectRetreatAreaStep();

protected:
    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector needRetreatKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector isRetreatingKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector movePositionKey;

    /**
     * 팀 이동 Task가 읽을 최종 Goal Actor Key입니다.
     * Normal과 Actor 없는 Jump/Drop은 내부 TargetPoint, Actor 기반 이동은 AreaLink의 Traversal Actor가 들어갑니다.
     */
    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector moveTargetActorKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector traversalTypeKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector traversalActorKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector entryPositionKey;

    UPROPERTY(EditAnywhere, Category = "Blackboard")
    FBlackboardKeySelector exitPositionKey;

    UPROPERTY(EditAnywhere, Category = "Route Settings")
    FAreaTraversalCapabilities traversalCapabilities;

    UPROPERTY(EditAnywhere, Category = "Route Settings", meta = (ClampMin = "0.0"))
    float minimumRiskImprovement = 0.01f;

    UPROPERTY(EditAnywhere, Category = "Route Settings", meta = (ClampMin = "0.0"))
    float riskEqualityTolerance = 0.01f;

    UPROPERTY(EditAnywhere, Category = "Route Settings", meta = (ClampMin = "0.0"))
    FVector navProjectionExtent = FVector(300.0, 300.0, 500.0);

    UPROPERTY(EditAnywhere, Category = "Route Settings", meta = (ClampMin = "0.0"))
    float candidateInset = 80.0f;

private:
    void ResetBlackboardOutputs(UBlackboardComponent& Blackboard) const;

    bool WriteFirstRouteStepToBlackboard(
        UBlackboardComponent& Blackboard,
        UWorld& World,
        AAIController& OwnerController,
        const FAreaRouteResult& RouteResult) const;

    AActor* GetOrCreateMoveTargetActor(
        UBlackboardComponent& Blackboard,
        UWorld& World,
        AAIController& OwnerController) const;
};
