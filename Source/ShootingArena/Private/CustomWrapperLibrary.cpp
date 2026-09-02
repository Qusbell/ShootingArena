

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


namespace
{
	// 비대칭 중력(GUp: 상승, GDown: 하강)에서, 수평 기준 발사각 AngleRad 을 고정했을 때
	// 수평거리 Dxy / 높이차 H 지점에 정확히 도달하는 초기 수직속도 Vz(>0) 를 구한다.
	// 그 각도로는 도달 불가능하면(목표가 조준선 위) false.
	bool SolveVzForLaunchAngle(float Dxy, float H, float GUp, float GDown, float AngleRad, float& OutVz)
	{
		const float TanA = FMath::Tan(AngleRad);
		if (TanA <= KINDA_SMALL_NUMBER || Dxy <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		// 물리적 상한: 궤적은 항상 조준선(y = x·tanθ) 아래에 있다.
		// 목표가 그보다 높으면 어떤 속도로도 그 각도로는 도달 불가.
		const float AimLineH = Dxy * TanA;
		if (H >= AimLineH)
		{
			return false;
		}

		// 목표를 최고점 이전(상승 중)에 맞추는지, 이후(하강 중)에 맞추는지의 경계는
		// H == AimLineH / 2 이다.
		if (H >= 0.5f * AimLineH)
		{
			// 상승 구간에서 명중 → 그 구간 중력은 GUp 뿐이라 닫힌 해가 존재한다.
			//   Vz^2 = GUp · Dxy^2 · tan^2θ / ( 2·(Dxy·tanθ - H) )
			const float Vz2 = (GUp * Dxy * Dxy * TanA * TanA) / (2.0f * (AimLineH - H));
			OutVz = FMath::Sqrt(FMath::Max(0.0f, Vz2));
			return true;
		}

		// 하강 구간에서 명중 → 상승 GUp / 하강 GDown 이 섞이므로 수치해(이분 탐색).
		// 수평 도달거리 오차: Vxy = Vz·cotθ, 비행시간 T = tUp + tDown, 이 구간에서 Vz 에 단조증가.
		const float CotA = 1.0f / TanA;
		auto FlightError = [=](float Vz) -> float
		{
			const float ApexAboveStart = (Vz * Vz) / (2.0f * GUp);
			const float Drop = ApexAboveStart - H; // 최고점에서 목표 높이까지의 낙하량
			if (Drop <= 0.0f)
			{
				return -Dxy; // 최고점이 목표보다 낮음 → 음수 반환으로 Vz 를 더 키우게 함
			}
			const float tUp = Vz / GUp;
			const float tDown = FMath::Sqrt(2.0f * Drop / GDown);
			return (Vz * CotA) * (tUp + tDown) - Dxy;
		};

		float Lo = FMath::Sqrt(2.0f * GUp * FMath::Max(0.0f, H)) + 1.0f;
		float Hi = FMath::Max(Lo, 1.0f);

		int32 Guard = 0;
		while (FlightError(Hi) < 0.0f)
		{
			Hi *= 2.0f;
			if (++Guard > 64)
			{
				return false; // 발산 방지 (정상 입력에서는 도달하지 않음)
			}
		}

		for (int32 i = 0; i < 60; ++i)
		{
			const float Mid = 0.5f * (Lo + Hi);
			if (FlightError(Mid) > 0.0f)
			{
				Hi = Mid;
			}
			else
			{
				Lo = Mid;
			}
		}
		OutVz = 0.5f * (Lo + Hi);
		return true;
	}
}


bool UCustomWrapperLibrary::SuggestJumpPadVelocity(
	UObject* WorldContextObject,
	FVector StartPos,
	FVector EndPos,
	FVector& OutLaunchVelocity,
	float RiseGravityScale,
	float FallGravityScale,
	float LaunchAngleDeg)
{
	OutLaunchVelocity = FVector::ZeroVector;

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
		: nullptr;
	if (!World)
	{
		return false;
	}

	// 상승/하강 각 구간의 실제 중력 가속도 크기 (양수)
	const float WorldG = FMath::Abs(World->GetGravityZ());
	const float GUp = WorldG * FMath::Abs(RiseGravityScale);
	const float GDown = WorldG * FMath::Abs(FallGravityScale);

	if (GUp <= KINDA_SMALL_NUMBER || GDown <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector Delta = EndPos - StartPos;
	const FVector DeltaXY(Delta.X, Delta.Y, 0.0f);
	const float Dxy = DeltaXY.Size();
	const float H = Delta.Z; // 시작점 대비 목표점의 높이차 (부호 있음)

	// 발사각 방식은 수평거리가 있어야 의미가 있다. (수직 발사가 필요하면 ApexTime 점프패드를 쓸 것)
	if (Dxy <= 1.0f)
	{
		return false;
	}

	const float AngleRad = FMath::DegreesToRadians(FMath::Clamp(LaunchAngleDeg, 1.0f, 89.0f));

	float VzUp = 0.0f;
	if (!SolveVzForLaunchAngle(Dxy, H, GUp, GDown, AngleRad, VzUp))
	{
		return false; // 그 각도로는 목표 지점에 도달할 수 없음
	}

	const FVector DirXY = DeltaXY / Dxy;
	const float Vxy = VzUp / FMath::Tan(AngleRad);

	OutLaunchVelocity = DirXY * Vxy + FVector(0.0f, 0.0f, VzUp);
	return true;
}


bool UCustomWrapperLibrary::SuggestJumpPadVelocityByApexTime(
	UObject* WorldContextObject,
	FVector StartPoint,
	FVector LaunchPosition,
	float ApexTime,
	FVector& OutLaunchVelocity,
	float GravityZOverride)
{
	OutLaunchVelocity = FVector::ZeroVector;

	if (ApexTime <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// 비행 중 적용될 중력 가속도 크기 (양수). Override 가 없으면 월드 중력에서 읽는다.
	float G = FMath::Abs(GravityZOverride);
	if (G <= KINDA_SMALL_NUMBER)
	{
		const UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
			: nullptr;
		if (!World)
		{
			return false;
		}
		G = FMath::Abs(World->GetGravityZ());
	}

	if (G <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector Delta = LaunchPosition - StartPoint;

	// 수평속도: ApexTime 동안 수평 변위를 모두 커버 (비행 내내 일정, 공기저항 없음)
	const float Vx = Delta.X / ApexTime;
	const float Vy = Delta.Y / ApexTime;

	// 수직속도: ApexTime 시점에 목표 높이에 도달하도록 중력 낙하량을 보정
	//   Delta.Z = Vz * ApexTime - 0.5 * G * ApexTime^2  →  Vz = (Delta.Z + 0.5*G*ApexTime^2) / ApexTime
	const float Vz = (Delta.Z + 0.5f * G * ApexTime * ApexTime) / ApexTime;

	OutLaunchVelocity = FVector(Vx, Vy, Vz);
	return true;
}