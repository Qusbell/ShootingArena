#include "LocalDedicatedServerLibrary.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

namespace
{
	// 실행 중인 로컬 전용 서버 프로세스 핸들. 캠페인 세션 동안 한 번에 하나만 존재한다고 가정합니다.
	FProcHandle GLocalDedicatedServerHandle;
}

bool ULocalDedicatedServerLibrary::StartLocalDedicatedServer(const FString& MapName, int32 Port)
{
	if (IsLocalDedicatedServerRunning())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LocalDedicatedServer] 이미 실행 중인 로컬 전용 서버가 있어 새로 시작하지 않습니다."));
		return false;
	}

	const FString ServerExePath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries"), TEXT("Win64"), TEXT("ShootingArenaServer.exe"));

	if (!FPaths::FileExists(ServerExePath))
	{
		UE_LOG(LogTemp, Error, TEXT("[LocalDedicatedServer] 서버 실행 파일을 찾을 수 없습니다: %s"), *ServerExePath);
		return false;
	}

	const FString Params = FString::Printf(TEXT("%s -server -log -port=%d"), *MapName, Port);

	uint32 OutProcessID = 0;
	GLocalDedicatedServerHandle = FPlatformProcess::CreateProc(
		*ServerExePath,
		*Params,
		true,	// bLaunchDetached: 부모(클라이언트) 프로세스와 독립적으로 실행 (StopLocalDedicatedServer가 종료를 책임짐)
		true,	// bLaunchHidden: 콘솔 창 숨김
		true,	// bLaunchReallyHidden: 태스크바에도 표시하지 않음
		&OutProcessID,
		0,
		nullptr,
		nullptr);

	if (!GLocalDedicatedServerHandle.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[LocalDedicatedServer] 서버 프로세스 실행에 실패했습니다: %s %s"), *ServerExePath, *Params);
	}

	return GLocalDedicatedServerHandle.IsValid();
}

void ULocalDedicatedServerLibrary::StopLocalDedicatedServer()
{
	if (GLocalDedicatedServerHandle.IsValid())
	{
		FPlatformProcess::TerminateProc(GLocalDedicatedServerHandle, true);
		FPlatformProcess::CloseProc(GLocalDedicatedServerHandle);
		GLocalDedicatedServerHandle.Reset();
	}
}

bool ULocalDedicatedServerLibrary::IsLocalDedicatedServerRunning()
{
	return GLocalDedicatedServerHandle.IsValid() && FPlatformProcess::IsProcRunning(GLocalDedicatedServerHandle);
}
