
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

	// --------------------------------------------------------------------
	// 매치 서버(로비 서버가 "게임 시작" 시 별도 프로세스로 띄우는 서버)용 함수들입니다.
	// 위 캠페인용 StartLocalDedicatedServer와 로직은 거의 같지만,
	// - "Dedicated Server 프로세스 안에서는 호출 금지" 가드가 없습니다 (로비 서버 자체가
	//   Dedicated Server이고, 그 안에서 이 함수를 호출하는 게 정상적인 사용 시나리오이기 때문).
	// - 위 캠페인용 핸들과는 별도의 핸들로 추적합니다 (서로 독립적인 프로세스).
	// Ready 확인은 캠페인용과 동일하게 MarkLocalDedicatedServerReady를 그대로 재사용합니다
	// (매치 서버 쪽 GameMode BeginPlay에서 그 함수를 그대로 호출하면 됩니다).
	// --------------------------------------------------------------------

	/**
	* 매치 서버 프로세스를 백그라운드로 실행합니다. 이미 실행 중인 매치 서버가 있다면
	* 아무 것도 하지 않고 false를 반환합니다.
	*
	* @param MapName 서버가 시작 시 로드할 맵 이름입니다. "?Game=..."이나 "?AIEasy=1" 같은
	*                URL 옵션을 그대로 이어붙여서 넘겨도 됩니다 (예: "Map_01?Game=BP_MultiplayerAIGameMode_C?AIEasy=1").
	* @param Port 매치 서버가 사용할 포트 번호
	*/
	UFUNCTION(BlueprintCallable, Category = "Server|MatchServer", meta = (Keywords = "Match Server Start Dedicated Process"))
	static bool StartMatchServer(const FString& MapName, int32 Port = 7778);

	/**
	* StartMatchServer로 띄운 매치 서버 프로세스를 종료합니다.
	* 실행 중인 프로세스가 없다면 아무 일도 일어나지 않습니다.
	*/
	UFUNCTION(BlueprintCallable, Category = "Server|MatchServer", meta = (Keywords = "Match Server Stop Dedicated Process"))
	static void StopMatchServer();

	/**
	* StartMatchServer로 띄운 매치 서버 프로세스가 현재 실행 중인지 확인합니다.
	*/
	UFUNCTION(BlueprintPure, Category = "Server|MatchServer", meta = (Keywords = "Match Server Running Dedicated Process"))
	static bool IsMatchServerRunning();

	/**
	* 매치 서버가 클라이언트 접속을 받을 준비가 되었는지 확인합니다.
	*/
	UFUNCTION(BlueprintPure, Category = "Server|MatchServer", meta = (Keywords = "Match Server Ready Dedicated Process"))
	static bool IsMatchServerReady();
};
