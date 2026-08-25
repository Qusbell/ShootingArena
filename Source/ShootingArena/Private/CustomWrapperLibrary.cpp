

#include "CustomWrapperLibrary.h"

#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#include "AIController.h"
#include "GameFramework/Pawn.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BTNode.h"

#include "Algo/Reverse.h"


ENavPathTestResult UCustomWrapperLibrary::TestPathExistsSync(
	UObject* WorldContextObject,
	FVector PathStart,
	FVector PathEnd,
	ANavigationData* NavData,
	TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	// Context Object 유효성 검사
	if (!WorldContextObject)
	{
		UE_LOG(LogNavigation, Warning, TEXT("TestPathExistsSync: WorldContextObject가 유효하지 않습니다."));
		return ENavPathTestResult::OffMesh;
	}

	// World 인스턴스 획득
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		return ENavPathTestResult::OffMesh;
	}

	// Navigation System 획득
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		UE_LOG(LogNavigation, Warning, TEXT("TestPathExistsSync: NavigationSystemV1을 찾을 수 없습니다."));
		return ENavPathTestResult::OffMesh;
	}

	// 대상 Navigation Data 지정 (전달받은 NavData가 없으면 기본 메인 NavMesh 사용)
	ANavigationData* TargetNavData = NavData ? NavData : NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	if (!TargetNavData)
	{
		UE_LOG(LogNavigation, Verbose, TEXT("TestPathExistsSync: 유효한 NavigationData가 활성화되어 있지 않습니다."));
		return ENavPathTestResult::OffMesh;
	}

	// PathStart 또는 PathEnd가 NavMesh 위에 존재하는지 검사
	const FVector Extent = TargetNavData->GetDefaultQueryExtent();
	FNavLocation NavStart, NavEnd;

	const bool bStartOnMesh = TargetNavData->ProjectPoint(PathStart, NavStart, Extent);
	const bool bEndOnMesh = TargetNavData->ProjectPoint(PathEnd, NavEnd, Extent);

	// 시작점 또는 목적지 중 하나라도 Mesh 위에 존재하지 않으면 OffMesh 반환
	if (!bStartOnMesh || !bEndOnMesh)
	{
		return ENavPathTestResult::OffMesh;
	}

	// Filter 지정
	FSharedConstNavQueryFilter QueryFilter = UNavigationQueryFilter::GetQueryFilter(*TargetNavData, WorldContextObject, FilterClass);

	// FPathFindingQuery 구조체 생성
	FPathFindingQuery Query(WorldContextObject, *TargetNavData, NavStart.Location, NavEnd.Location, QueryFilter);

	Query.SetAllowPartialPaths(false);
	Query.SetRequireNavigableEndLocation(true);

	// TestPathSync 호출 (경로 객체를 할당하지 않고 Island 판정만 빠르게 수행)
	const bool bPathExists = NavSys->TestPathSync(Query, EPathFindingMode::Hierarchical);

	// 경로가 연결되어 있으면 PathExists, 거리가 너무 멀거나 단절되어 있으면 Islands 반환
	return bPathExists ? ENavPathTestResult::PathExists : ENavPathTestResult::Islands;
}


bool UCustomWrapperLibrary::GetActiveBehaviorTreeNode(
	AActor* TargetActor,
	FString& OutBehaviorTreeName,
	FString& OutActiveNodeName)
{
	OutBehaviorTreeName.Reset();
	OutActiveNodeName.Reset();

	if (!TargetActor)
	{
		return false;
	}

	// TargetActor가 AIController 자체인 경우
	AAIController* AIController = Cast<AAIController>(TargetActor);

	// TargetActor가 Pawn인 경우 해당 Pawn의 Controller를 가져옴
	if (!AIController)
	{
		if (APawn* Pawn = Cast<APawn>(TargetActor))
		{
			AIController = Cast<AAIController>(Pawn->GetController());
		}
	}

	if (!AIController)
	{
		return false;
	}

	// AIController가 현재 사용하고 있는 BrainComponent가
	// BehaviorTreeComponent인지 확인
	UBehaviorTreeComponent* BTComponent =
		Cast<UBehaviorTreeComponent>(AIController->GetBrainComponent());

	if (!BTComponent)
	{
		return false;
	}

	// 현재 실행 중인 Behavior Tree
	if (UBehaviorTree* CurrentTree = BTComponent->GetCurrentTree())
	{
		OutBehaviorTreeName = CurrentTree->GetName();
	}

	// 현재 Active Node
	const UBTNode* ActiveNode = BTComponent->GetActiveNode();

	if (!ActiveNode)
	{
		return false;
	}

	OutActiveNodeName = ActiveNode->GetNodeName();

	return true;
}

bool UCustomWrapperLibrary::GetBehaviorTreeDebugInfo(
	AActor* TargetActor,
	FString& OutBehaviorTreeName,
	FString& OutActiveNodeName,
	FString& OutActiveTasks)
{
	OutBehaviorTreeName.Reset();
	OutActiveNodeName.Reset();
	OutActiveTasks.Reset();

	if (!TargetActor)
	{
		return false;
	}

	AAIController* AIController = Cast<AAIController>(TargetActor);

	if (!AIController)
	{
		if (APawn* Pawn = Cast<APawn>(TargetActor))
		{
			AIController = Cast<AAIController>(Pawn->GetController());
		}
	}

	if (!AIController)
	{
		return false;
	}

	UBehaviorTreeComponent* BTComponent =
		Cast<UBehaviorTreeComponent>(AIController->GetBrainComponent());

	if (!BTComponent)
	{
		return false;
	}

	// 현재 Behavior Tree
	if (UBehaviorTree* CurrentTree = BTComponent->GetCurrentTree())
	{
		OutBehaviorTreeName = CurrentTree->GetName();
	}

	// 일반적인 현재 Active Node
	if (const UBTNode* ActiveNode = BTComponent->GetActiveNode())
	{
		OutActiveNodeName = ActiveNode->GetNodeName();
	}

	// 현재 실행 중인 Task들.
	// Parallel Task가 있다면 그것까지 포함된 디버그 정보를 얻는 용도.
	OutActiveTasks = BTComponent->DescribeActiveTasks();

	return true;
}

bool UCustomWrapperLibrary::GetActiveBehaviorTreePath(
	AActor* TargetActor,
	TArray<FString>& OutNodePath)
{
	OutNodePath.Reset();

	if (!TargetActor)
	{
		return false;
	}

	AAIController* AIController = Cast<AAIController>(TargetActor);

	if (!AIController)
	{
		if (APawn* Pawn = Cast<APawn>(TargetActor))
		{
			AIController = Cast<AAIController>(Pawn->GetController());
		}
	}

	if (!AIController)
	{
		return false;
	}

	UBehaviorTreeComponent* BTComponent =
		Cast<UBehaviorTreeComponent>(AIController->GetBrainComponent());

	if (!BTComponent)
	{
		return false;
	}

	const UBTNode* CurrentNode = BTComponent->GetActiveNode();

	if (!CurrentNode)
	{
		return false;
	}

	// Leaf -> Root 방향으로 수집
	while (CurrentNode)
	{
		OutNodePath.Add(CurrentNode->GetNodeName());

		CurrentNode = CurrentNode->GetParentNode();
	}

	// Root -> Leaf 순서로 변경
	Algo::Reverse(OutNodePath);

	return OutNodePath.Num() > 0;
}


bool UCustomWrapperLibrary::GetPeakKeyFromRuntimeFloatCurve(
	const FRuntimeFloatCurve& Curve,
	float& OutPeakTime,
	float& OutPeakValue)
{
	const FRichCurve* RichCurve = Curve.GetRichCurveConst();

	if (!RichCurve)
	{
		OutPeakTime = 0.0f;
		OutPeakValue = 0.0f;
		return false;
	}

	const TArray<FRichCurveKey>& Keys = RichCurve->GetConstRefOfKeys();

	if (Keys.IsEmpty())
	{
		OutPeakTime = 0.0f;
		OutPeakValue = 0.0f;
		return false;
	}

	OutPeakTime = Keys[0].Time;
	OutPeakValue = Keys[0].Value;

	for (int32 i = 1; i < Keys.Num(); ++i)
	{
		const FRichCurveKey& Key = Keys[i];

		if (Key.Value > OutPeakValue)
		{
			OutPeakValue = Key.Value;
			OutPeakTime = Key.Time;
		}
		else if (FMath::IsNearlyEqual(Key.Value, OutPeakValue) &&
			Key.Time < OutPeakTime)
		{
			// 최고값이 같으면 더 가까운 거리 선택
			OutPeakTime = Key.Time;
		}
	}

	return true;
}