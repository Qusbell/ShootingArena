
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "LocalDedicatedServerLibrary.generated.h"

/**
 * 캠페인 진입 시 로컬에 전용 서버(ShootingArenaServer.exe) 프로세스를 띄우고,
 * 캠페인 종료 시 그 프로세스를 정리하기 위한 래핑 라이브러리.
 */
UCLASS()
class SHOOTINGARENA_API ULocalDedicatedServerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	* 로컬 전용 서버 프로세스(Binaries/Win64/ShootingArenaServer.exe)를 백그라운드로 실행합니다.
	* 이미 실행 중인 로컬 전용 서버가 있다면 아무 것도 하지 않고 false를 반환합니다.
	*
	* @param MapName 서버가 시작 시 로드할 맵 이름 (예: "Campaign_Map_01")
	* @param Port 서버가 사용할 포트 번호 (기본값 7777)
	* @return 프로세스 생성에 성공하면 true
	*/
	UFUNCTION(BlueprintCallable, Category = "Server|LocalDedicatedServer", meta = (Keywords = "Campaign Server Start Dedicated Process"))
	static bool StartLocalDedicatedServer(const FString& MapName, int32 Port = 7777);

	/**
	* StartLocalDedicatedServer로 띄운 로컬 전용 서버 프로세스를 종료합니다.
	* 실행 중인 프로세스가 없다면 아무 일도 일어나지 않습니다.
	*/
	UFUNCTION(BlueprintCallable, Category = "Server|LocalDedicatedServer", meta = (Keywords = "Campaign Server Stop Dedicated Process"))
	static void StopLocalDedicatedServer();

	/**
	* StartLocalDedicatedServer로 띄운 로컬 전용 서버 프로세스가 현재 실행 중인지 확인합니다.
	*/
	UFUNCTION(BlueprintPure, Category = "Server|LocalDedicatedServer", meta = (Keywords = "Campaign Server Running Dedicated Process"))
	static bool IsLocalDedicatedServerRunning();


	/**
	* 로컬 전용 서버가 클라이언트 접속을 받을 준비가 되었는지 확인합니다.
	*
	* 서버 프로세스가 살아 있고,
	* 해당 서버 프로세스가 Ready 파일을 생성한 경우 true를 반환합니다.
	*/
	UFUNCTION(BlueprintPure, Category = "Server|LocalDedicatedServer",
		meta = (Keywords = "Campaign Server Ready Dedicated Process"))
	static bool IsLocalDedicatedServerReady();

	/**
	 * 현재 Dedicated Server 프로세스가 클라이언트 접속 준비를 완료했음을 표시합니다.
	 *
	 * 서버 측 ServerEntry GameMode 등에서 호출합니다.
	 * Dedicated Server가 아니거나 NetDriver가 아직 준비되지 않았다면 false를 반환합니다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Server|LocalDedicatedServer",
		meta = (
			WorldContext = "WorldContextObject",
			Keywords = "Campaign Server Ready Mark Dedicated Process"
			))
	static bool MarkLocalDedicatedServerReady(UObject* WorldContextObject);
};
