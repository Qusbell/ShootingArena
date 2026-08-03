#include "AI/Area/BTS_UpdateRetreatDecision.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "AI/Area/AIAreaBase.h"
#include "AI/Area/AreaManagerSubsystem.h"
#include "AI/Area/Internal/AreaRetreatPlanner.h"

UBTS_UpdateRetreatDecision::UBTS_UpdateRetreatDecision()
{
    NodeName = TEXT("Update Retreat Decision");

    bNotifyBecomeRelevant = true;
    bNotifyTick = true;

    // 매 프레임 경로를 다시 계산하지 않고, 행동 판단 주기로만 갱신합니다.
    Interval = 0.5f;
    RandomDeviation = 0.1f;

    needRetreatKey.AddBoolFilter(
        this,
        GET_MEMBER_NAME_CHECKED(UBTS_UpdateRetreatDecision, needRetreatKey));

    isRetreatingKey.AddBoolFilter(
        this,
        GET_MEMBER_NAME_CHECKED(UBTS_UpdateRetreatDecision, isRetreatingKey));

    // 새 Service를 배치하면 현재 프로젝트 Blackboard 이름을 기본으로 사용합니다.
    needRetreatKey.SelectedKeyName = TEXT("needRetreat");
    isRetreatingKey.SelectedKeyName = TEXT("isRetreating");

    // AreaLink에 실제 실행 Actor가 지정된 모든 이동 종류를 자동 후퇴 후보로 사용합니다.
    traversalCapabilities.bCanUseNormal = true;
    traversalCapabilities.bCanUseTeleport = true;
    traversalCapabilities.bCanUseJumpPad = true;
    traversalCapabilities.bCanUseDrop = true;
    traversalCapabilities.bCanUseJump = true;
    traversalCapabilities.bCanUseDoor = true;
}

void UBTS_UpdateRetreatDecision::OnBecomeRelevant(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    Super::OnBecomeRelevant(OwnerComp, NodeMemory);
    UpdateRetreatDecision(OwnerComp);
}

void UBTS_UpdateRetreatDecision::TickNode(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
    UpdateRetreatDecision(OwnerComp);
}

void UBTS_UpdateRetreatDecision::UpdateRetreatDecision(
    UBehaviorTreeComponent& OwnerComp) const
{
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    AAIController* OwnerController = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = OwnerController ? OwnerController->GetPawn() : nullptr;
    UWorld* World = OwnerComp.GetWorld();

    if (!Blackboard || !OwnerController || !ControlledPawn || !World)
    {
        return;
    }

    UAreaManagerSubsystem* AreaSubsystem =
        World->GetSubsystem<UAreaManagerSubsystem>();

    if (!AreaSubsystem || !AreaSubsystem->IsGraphReady())
    {
        Blackboard->SetValueAsBool(needRetreatKey.SelectedKeyName, false);
        Blackboard->SetValueAsBool(isRetreatingKey.SelectedKeyName, false);
        return;
    }

    // Area 경계를 통과하는 짧은 순간에는 어느 Area에도 속하지 않을 수 있습니다.
    // 이미 이동 중이라면 그 순간만으로 후퇴를 중단하지 않습니다.
    AAIAreaBase* CurrentArea = AreaSubsystem->GetAreaByPosition(
        ControlledPawn->GetActorLocation());

    if (!IsValid(CurrentArea))
    {
        if (!Blackboard->GetValueAsBool(isRetreatingKey.SelectedKeyName))
        {
            Blackboard->SetValueAsBool(needRetreatKey.SelectedKeyName, false);
        }
        return;
    }

    FAreaRetreatPlannerSettings Settings;
    Settings.TraversalCapabilities = traversalCapabilities;
    Settings.MinimumRiskImprovement = minimumRiskImprovement;
    Settings.RiskEqualityTolerance = riskEqualityTolerance;
    Settings.NavProjectionExtent = navProjectionExtent;
    Settings.CandidateInset = candidateInset;

    FAreaRetreatPlan RetreatPlan;
    const bool bShouldRetreat = FAreaRetreatPlanner::FindBestRetreatPlan(
        *World,
        *AreaSubsystem,
        *OwnerController,
        *ControlledPawn,
        Settings,
        RetreatPlan);

    Blackboard->SetValueAsBool(
        needRetreatKey.SelectedKeyName,
        bShouldRetreat);

    if (!bShouldRetreat)
    {
        Blackboard->SetValueAsBool(isRetreatingKey.SelectedKeyName, false);
    }
}
