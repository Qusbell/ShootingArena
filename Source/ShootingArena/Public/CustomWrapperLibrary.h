
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

};
