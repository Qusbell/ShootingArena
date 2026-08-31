
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "NavigationData.h"
#include "NavFilters/NavigationQueryFilter.h"

#include "CustomWrapperLibrary.generated.h"




UENUM(BlueprintType)
enum class ENavPathTestResult : uint8
{
	PathExists  UMETA(DisplayName = "Connected Path"),
	Islands     UMETA(DisplayName = "Disconnected Islands"),
	OffMesh     UMETA(DisplayName = "Point OffMesh")
};




/** 점프패드 궤적을 무엇으로 결정할지 선택 */
UENUM(BlueprintType)
enum class EJumpPadArcMode : uint8
{
	/** 최고점 여유 높이(ApexHeight)로 궤적을 결정. 발사각은 결과로 따라옴 (기존 방식) */
	ApexHeight   UMETA(DisplayName = "Apex Height"),
	/** 발사각(LaunchAngle, 수평 기준)을 고정하고 도달에 필요한 속도를 역산 */
	LaunchAngle  UMETA(DisplayName = "Launch Angle")
};




/**
 * cpp에만 존재하는 기능들을 BP에서도 사용할 수 있도록 래핑하는 라이브러리
 */
UCLASS()
class SHOOTINGARENA_API UCustomWrapperLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:

	/**
	* 시작 지점과 목적지 사이에 유효한 네비게이션 경로가 존재하는지 동기식(Sync)으로 빠르게 검사합니다.
	* 경로 객체를 생성하지 않으므로 FindPath보다 연산 비용이 매우 적습니다.
	* 현재 Hierarchical을 기반으로 구현되고 있으므로, FilterClass는 실질적으로 무시되고 있습니다.
	*
	* @param WorldContextObject 월드 컨텍스트를 가져오기 위한 객체 (블루프린트에서 Self 지정)
	* @param PathStart 경로 탐색 시작 위치 (World Space)
	* @param PathEnd 경로 탐색 목적지 위치 (World Space)
	* @param NavData 특정 NavMesh Data 지정 (nullptr 지정 시 기본 NavMesh 사용)
	* @param FilterClass 커스텀 Navigation Query Filter 클래스 (선택 사항)
	* @return 경로가 존재하고 막혀있지 않다면 true, 경로가 없거나 막혔다면 false 반환
	*/
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation", meta = (WorldContext = "WorldContextObject", Keywords = "TestPath PathExists CanNavigate NavMesh Navigation"))
	static ENavPathTestResult TestPathExistsSync(
		UObject* WorldContextObject,
		FVector PathStart,
		FVector PathEnd,
		ANavigationData* NavData = nullptr,
		TSubclassOf<UNavigationQueryFilter> FilterClass = nullptr
	);

	/**
    * AIController 또는 AI Pawn이 현재 실행 중인 Behavior Tree의
    * Active Node 이름을 반환합니다.
    *
    * @param TargetActor AIController 또는 AIController가 소유한 Pawn
    * @param OutBehaviorTreeName 현재 실행 중인 Behavior Tree 이름
    * @param OutActiveNodeName 현재 Active Node 이름
    * @return 유효한 BehaviorTreeComponent와 Active Node를 찾았으면 true
    */
	UFUNCTION(BlueprintPure, Category = "AI|BehaviorTree", meta = (Keywords = "BehaviorTree BT Active Node Current Debug AI"))
	static bool GetActiveBehaviorTreeNode(
		AActor* TargetActor,
		FString& OutBehaviorTreeName,
		FString& OutActiveNodeName
	);

	UFUNCTION(BlueprintPure, Category = "AI|BehaviorTree", meta = (Keywords = "BehaviorTree BT Active Tasks Parallel Current Debug AI"))
	static bool GetBehaviorTreeDebugInfo(
		AActor* TargetActor,
		FString& OutBehaviorTreeName,
		FString& OutActiveNodeName,
		FString& OutActiveTasks
	);

	UFUNCTION(BlueprintPure, Category = "AI|BehaviorTree", meta = (Keywords = "BehaviorTree BT Active Node Path Current Debug AI"))
	static bool GetActiveBehaviorTreePath(
		AActor* TargetActor,
		TArray<FString>& OutNodePath
	);



	UFUNCTION(BlueprintPure, Category = "AI|Curve")
	static bool GetPeakKeyFromRuntimeFloatCurve(
		const FRuntimeFloatCurve& Curve,
		float& OutPeakTime,
		float& OutPeakValue
	);


	/**
	 * 점프패드용: StartPos -> EndPos 에 정확히 도달하는 발사 속도를 계산합니다.
	 * 상승 구간(Vz>0)과 하강 구간(Vz<=0)의 GravityScale 을 다르게 줄 수 있어
	 * "빠르게 솟구쳐서 느긋하게 낙하" 같은 비대칭 포물선을 목표 착지 정확도를 유지한 채 만듭니다.
	 * 실제로 이 궤적을 타려면 런타임에서 캐릭터가 Vz 부호에 따라 GravityScale 을
	 * Rise/Fall 로 전환해야 합니다 (UJumpPadFlightComponent).
	 *
	 * StartPos == EndPos 수평거리가 0이면 수직으로만 발사됩니다.
	 *
	 * @param WorldContextObject   월드 컨텍스트 (BP 에서 Self)
	 * @param StartPos             발사 시작 위치 (보통 캐릭터 위치)
	 * @param EndPos               목표 착지 위치
	 * @param OutLaunchVelocity    LaunchCharacter 에 넣을 발사 속도 (bXYOverride / bZOverride = true)
	 * @param RiseGravityScale     상승 중 적용할 GravityScale. 클수록 빠르게 솟고 수평속도도 빨라짐
	 * @param FallGravityScale     하강 중 적용할 GravityScale. 작을수록 느긋하게 낙하
	 * @param ApexHeight           [ApexHeight 모드] 더 높은 끝점 기준 최고점 여유 높이(cm). 클수록 붕 뜨는 궤적
	 * @param ArcMode              궤적 결정 방식. ApexHeight(기존) 또는 LaunchAngle
	 * @param LaunchAngleDeg       [LaunchAngle 모드] 수평 기준 발사각(도, 1~89). 클수록 가파르게 솟는 궤적.
	 *                             그 각도로 목표에 도달할 수 없으면 false 를 반환한다.
	 *                             수평거리가 거의 0이면 이 모드는 무시되고 ApexHeight 로 폴백한다.
	 * @return 유효한 궤적이 나오면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameplay|JumpPad",
		meta = (WorldContext = "WorldContextObject",
			Keywords = "jumppad launch ballistic arc suggest projectile velocity gravity angle"))
	static bool SuggestJumpPadVelocity(
		UObject* WorldContextObject,
		FVector StartPos,
		FVector EndPos,
		FVector& OutLaunchVelocity,
		float RiseGravityScale = 3.0f,
		float FallGravityScale = 0.8f,
		float ApexHeight = 250.0f,
		EJumpPadArcMode ArcMode = EJumpPadArcMode::ApexHeight,
		float LaunchAngleDeg = 60.0f
	);

};
