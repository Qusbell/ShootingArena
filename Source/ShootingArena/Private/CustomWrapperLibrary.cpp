

#include "CustomWrapperLibrary.h"

#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"


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
	FPathFindingQuery Query(WorldContextObject, *TargetNavData, PathStart, PathEnd, QueryFilter);

	// TestPathSync 호출 (경로 객체를 할당하지 않고 Island 판정만 빠르게 수행)
	const bool bPathExists = NavSys->TestPathSync(Query, EPathFindingMode::Hierarchical);

	// 경로가 연결되어 있으면 PathExists, 거리가 너무 멀거나 단절되어 있으면 Islands 반환
	return bPathExists ? ENavPathTestResult::PathExists : ENavPathTestResult::Islands;
}
