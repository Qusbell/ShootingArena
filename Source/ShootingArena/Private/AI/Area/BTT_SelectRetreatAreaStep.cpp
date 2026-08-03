#include "AI/Area/BTT_SelectRetreatAreaStep.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

#include "AI/Area/AreaManagerSubsystem.h"
#include "AI/Area/Internal/AreaRetreatPlanner.h"

UBTT_SelectRetreatAreaStep::UBTT_SelectRetreatAreaStep()
{
    NodeName = TEXT("Select Retreat Area Step");

    needRetreatKey.AddBoolFilter(
        this,
        GET_MEMBER_NAME_CHECKED(UBTT_SelectRetreatAreaStep, needRetreatKey));

    isRetreatingKey.AddBoolFilter(
        this,
        GET_MEMBER_NAME_CHECKED(UBTT_SelectRetreatAreaStep, isRetreatingKey));

    movePositionKey.AddVectorFilter(
        this,
        GET_MEMBER_NAME_CHECKED(UBTT_SelectRetreatAreaStep, movePositionKey));

    moveTargetActorKey.AddObjectFilter(
        this,
        GET_MEMBER_NAME_CHECKED(UBTT_SelectRetreatAreaStep, moveTargetActorKey),
        AActor::StaticClass());

    traversalTypeKey.AddEnumFilter(
        this,
        GET_MEMBER_NAME_CHECKED(UBTT_SelectRetreatAreaStep, traversalTypeKey),
        StaticEnum<EAreaTraversalType>());

    traversalActorKey.AddObjectFilter(
        this,
        GET_MEMBER_NAME_CHECKED(UBTT_SelectRetreatAreaStep, traversalActorKey),
        AActor::StaticClass());
    traversalActorKey.AllowNoneAsValue(true);

    entryPositionKey.AddVectorFilter(
        this,
        GET_MEMBER_NAME_CHECKED(UBTT_SelectRetreatAreaStep, entryPositionKey));

    exitPositionKey.AddVectorFilter(
        this,
        GET_MEMBER_NAME_CHECKED(UBTT_SelectRetreatAreaStep, exitPositionKey));

    // 현재 프로젝트 Blackboard 이름을 기본값으로 지정합니다.
    needRetreatKey.SelectedKeyName = TEXT("needRetreat");
    isRetreatingKey.SelectedKeyName = TEXT("isRetreating");
    movePositionKey.SelectedKeyName = TEXT("retreatMovePosition");
    moveTargetActorKey.SelectedKeyName = TEXT("retreatMoveTarget");
    traversalTypeKey.SelectedKeyName = TEXT("retreatTraversalType");
    traversalActorKey.SelectedKeyName = TEXT("retreatTraversalActor");
    entryPositionKey.SelectedKeyName = TEXT("retreatEntryPosition");
    exitPositionKey.SelectedKeyName = TEXT("retreatExitPosition");

    // AreaLink에 실행 Actor만 정상 지정되어 있으면 모든 연결 종류를 바로 사용할 수 있습니다.
    traversalCapabilities.bCanUseNormal = true;
    traversalCapabilities.bCanUseTeleport = true;
    traversalCapabilities.bCanUseJumpPad = true;
    traversalCapabilities.bCanUseDrop = true;
    traversalCapabilities.bCanUseJump = true;
    traversalCapabilities.bCanUseDoor = true;
}

EBTNodeResult::Type UBTT_SelectRetreatAreaStep::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory)
{
    (void)NodeMemory;

    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    AAIController* OwnerController = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = OwnerController ? OwnerController->GetPawn() : nullptr;
    UWorld* World = OwnerComp.GetWorld();

    if (!Blackboard || !OwnerController || !ControlledPawn || !World)
    {
        return EBTNodeResult::Failed;
    }

    ResetBlackboardOutputs(*Blackboard);

    if (!Blackboard->GetValueAsBool(needRetreatKey.SelectedKeyName))
    {
        return EBTNodeResult::Failed;
    }

    UAreaManagerSubsystem* AreaSubsystem =
        World->GetSubsystem<UAreaManagerSubsystem>();

    if (!AreaSubsystem || !AreaSubsystem->IsGraphReady())
    {
        Blackboard->SetValueAsBool(needRetreatKey.SelectedKeyName, false);
        return EBTNodeResult::Failed;
    }

    FAreaRetreatPlannerSettings Settings;
    Settings.TraversalCapabilities = traversalCapabilities;
    Settings.MinimumRiskImprovement = minimumRiskImprovement;
    Settings.RiskEqualityTolerance = riskEqualityTolerance;
    Settings.NavProjectionExtent = navProjectionExtent;
    Settings.CandidateInset = candidateInset;

    FAreaRetreatPlan RetreatPlan;
    if (!FAreaRetreatPlanner::FindBestRetreatPlan(
            *World,
            *AreaSubsystem,
            *OwnerController,
            *ControlledPawn,
            Settings,
            RetreatPlan))
    {
        Blackboard->SetValueAsBool(needRetreatKey.SelectedKeyName, false);
        return EBTNodeResult::Failed;
    }

    if (!WriteFirstRouteStepToBlackboard(
            *Blackboard,
            *World,
            *OwnerController,
            RetreatPlan.RouteResult))
    {
        ResetBlackboardOutputs(*Blackboard);
        Blackboard->SetValueAsBool(needRetreatKey.SelectedKeyName, false);
        return EBTNodeResult::Failed;
    }

    return EBTNodeResult::Succeeded;
}

void UBTT_SelectRetreatAreaStep::ResetBlackboardOutputs(
    UBlackboardComponent& Blackboard) const
{
    Blackboard.SetValueAsBool(isRetreatingKey.SelectedKeyName, false);
    Blackboard.SetValueAsVector(
        movePositionKey.SelectedKeyName,
        FVector::ZeroVector);
    Blackboard.SetValueAsEnum(
        traversalTypeKey.SelectedKeyName,
        static_cast<uint8>(EAreaTraversalType::Normal));
    Blackboard.ClearValue(traversalActorKey.SelectedKeyName);
    Blackboard.SetValueAsVector(
        entryPositionKey.SelectedKeyName,
        FVector::ZeroVector);
    Blackboard.SetValueAsVector(
        exitPositionKey.SelectedKeyName,
        FVector::ZeroVector);
}

bool UBTT_SelectRetreatAreaStep::WriteFirstRouteStepToBlackboard(
    UBlackboardComponent& Blackboard,
    UWorld& World,
    AAIController& OwnerController,
    const FAreaRouteResult& RouteResult) const
{
    if (RouteResult.RouteSteps.IsEmpty())
    {
        return false;
    }

    const FAreaRouteStep& FirstStep = RouteResult.RouteSteps[0];
    const bool bIsNormal =
        FirstStep.TraversalType == EAreaTraversalType::Normal;

    AActor* MoveTargetActor = nullptr;
    FVector MovePosition = FVector::ZeroVector;

    if (bIsNormal)
    {
        // 일반 연결에는 실제 실행 Actor가 없으므로 재사용 가능한 숨겨진 TargetPoint를 사용합니다.
        MovePosition = FirstStep.ExitLocation;
        MoveTargetActor = GetOrCreateMoveTargetActor(
            Blackboard,
            World,
            OwnerController);

        if (!IsValid(MoveTargetActor)
            || !MoveTargetActor->SetActorLocation(
                MovePosition,
                false,
                nullptr,
                ETeleportType::TeleportPhysics))
        {
            return false;
        }
    }
    else if (IsValid(FirstStep.TraversalActor.Get()))
    {
        // 실제 Actor가 있는 특수 연결은 그 Actor 자체를 팀 이동 시스템의 Goal Actor로 넘깁니다.
        MoveTargetActor = FirstStep.TraversalActor.Get();
        MovePosition = MoveTargetActor->GetActorLocation();
    }
    else if (FirstStep.TraversalType == EAreaTraversalType::Jump
        || FirstStep.TraversalType == EAreaTraversalType::Drop)
    {
        // Jump/Drop은 실행 Actor가 없어도 됩니다.
        // 구역 Link가 계산한 반대편 Exit 위치에 내부 TargetPoint를 배치하고,
        // 팀 이동 시스템에는 그 Actor만 Goal Actor로 전달합니다.
        MovePosition = FirstStep.ExitLocation;
        MoveTargetActor = GetOrCreateMoveTargetActor(
            Blackboard,
            World,
            OwnerController);

        if (!IsValid(MoveTargetActor)
            || !MoveTargetActor->SetActorLocation(
                MovePosition,
                false,
                nullptr,
                ETeleportType::TeleportPhysics))
        {
            return false;
        }
    }
    else
    {
        // JumpPad, Teleport, Door는 실제 상호작용 Actor가 반드시 필요합니다.
        return false;
    }

    Blackboard.SetValueAsVector(
        movePositionKey.SelectedKeyName,
        MovePosition);
    Blackboard.SetValueAsObject(
        moveTargetActorKey.SelectedKeyName,
        MoveTargetActor);
    Blackboard.SetValueAsEnum(
        traversalTypeKey.SelectedKeyName,
        static_cast<uint8>(FirstStep.TraversalType));
    Blackboard.SetValueAsObject(
        traversalActorKey.SelectedKeyName,
        FirstStep.TraversalActor.Get());
    Blackboard.SetValueAsVector(
        entryPositionKey.SelectedKeyName,
        FirstStep.EntryLocation);
    Blackboard.SetValueAsVector(
        exitPositionKey.SelectedKeyName,
        FirstStep.ExitLocation);
    Blackboard.SetValueAsBool(
        isRetreatingKey.SelectedKeyName,
        true);

    return true;
}

AActor* UBTT_SelectRetreatAreaStep::GetOrCreateMoveTargetActor(
    UBlackboardComponent& Blackboard,
    UWorld& World,
    AAIController& OwnerController) const
{
    UObject* StoredObject = Blackboard.GetValueAsObject(
        moveTargetActorKey.SelectedKeyName);

    static const FName RetreatTargetTag(TEXT("AreaRetreatLocationMoveTarget"));

    ATargetPoint* TargetPoint = Cast<ATargetPoint>(StoredObject);
    if (IsValid(TargetPoint)
        && TargetPoint->GetOwner() == &OwnerController
        && TargetPoint->ActorHasTag(RetreatTargetTag))
    {
        return TargetPoint;
    }

    // 직전 Step이 특수 이동이었다면 Blackboard에는 실제 Traversal Actor가 들어 있습니다.
    // Controller가 이미 소유한 Normal용 TargetPoint를 찾아 재사용해 불필요한 Actor 누적을 막습니다.
    for (TActorIterator<ATargetPoint> It(&World); It; ++It)
    {
        ATargetPoint* ExistingTarget = *It;
        if (IsValid(ExistingTarget)
            && ExistingTarget->GetOwner() == &OwnerController
            && ExistingTarget->ActorHasTag(RetreatTargetTag))
        {
            Blackboard.SetValueAsObject(
                moveTargetActorKey.SelectedKeyName,
                ExistingTarget);
            return ExistingTarget;
        }
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Owner = &OwnerController;
    SpawnParameters.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParameters.ObjectFlags |= RF_Transient;

    TargetPoint = World.SpawnActor<ATargetPoint>(
        ATargetPoint::StaticClass(),
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParameters);

    if (!IsValid(TargetPoint))
    {
        return nullptr;
    }

    TargetPoint->Tags.AddUnique(RetreatTargetTag);
    TargetPoint->SetActorHiddenInGame(true);
    TargetPoint->SetActorEnableCollision(false);
    TargetPoint->SetReplicates(false);
    TargetPoint->SetActorTickEnabled(false);

    Blackboard.SetValueAsObject(
        moveTargetActorKey.SelectedKeyName,
        TargetPoint);

    return TargetPoint;
}
