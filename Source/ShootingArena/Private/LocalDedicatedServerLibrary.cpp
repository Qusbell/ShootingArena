#include "LocalDedicatedServerLibrary.h"

#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace
{
	// 실행 중인 로컬 전용 서버 프로세스 핸들.
	FProcHandle GLocalDedicatedServerHandle;

	// 현재 로컬 서버 실행에 대응하는 Ready 파일의 절대경로.
	// 클라이언트 프로세스에서만 의미 있는 값입니다.
	FString GLocalDedicatedServerReadyFilePath;

	constexpr TCHAR LocalServerReadyArgument[] =
		TEXT("LocalServerReadyFile=");
}



bool ULocalDedicatedServerLibrary::StartLocalDedicatedServer(
    const FString& MapName,
    int32 Port)
{
    if (IsLocalDedicatedServerRunning())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[LocalDedicatedServer] 이미 실행 중인 로컬 전용 서버가 있어 새로 시작하지 않습니다.")
        );

        return false;
    }

    // 이전에 유효하지 않게 된 핸들이 남아 있다면 정리합니다.
    if (GLocalDedicatedServerHandle.IsValid())
    {
        FPlatformProcess::CloseProc(GLocalDedicatedServerHandle);
        GLocalDedicatedServerHandle.Reset();
    }

    // --------------------------------------------------------------------
    // Ready 파일 경로 생성
    //
    // Saved/LocalDedicatedServer/Ready_<GUID>.flag
    //
    // 실행마다 GUID를 새로 만들어 이전 실행의 Ready 파일과 절대 충돌하지
    // 않도록 합니다.
    // --------------------------------------------------------------------

    const FString ReadyDirectory = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("LocalDedicatedServer")
        )
    );

    if (!IFileManager::Get().MakeDirectory(*ReadyDirectory, true))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[LocalDedicatedServer] Ready 디렉터리를 생성하지 못했습니다: %s"),
            *ReadyDirectory
        );

        return false;
    }

    const FString ReadyFileName = FString::Printf(
        TEXT("Ready_%s.flag"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits)
    );

    GLocalDedicatedServerReadyFilePath = FPaths::Combine(
        ReadyDirectory,
        ReadyFileName
    );

    // 혹시 동일 경로에 파일이 존재한다면 제거합니다.
    IFileManager::Get().Delete(
        *GLocalDedicatedServerReadyFilePath,
        false,
        true
    );

    // --------------------------------------------------------------------
    // 서버 실행 파일
    // --------------------------------------------------------------------

    const FString ServerExePath = FPaths::Combine(
        FPaths::EngineDir(),
        TEXT("Binaries"),
        TEXT("Win64"),
        TEXT("UnrealEditor.exe")
    );

    if (!FPaths::FileExists(ServerExePath))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[LocalDedicatedServer] 서버 실행 파일을 찾을 수 없습니다: %s"),
            *ServerExePath
        );

        GLocalDedicatedServerReadyFilePath.Reset();
        return false;
    }

    const FString ProjectFilePath =
        FPaths::ConvertRelativePathToFull(
            FPaths::GetProjectFilePath()
        );

    // Ready 파일의 "절대경로"를 서버 프로세스에 전달합니다.
    const FString Params = FString::Printf(
        TEXT("\"%s\" %s -game -server -log -port=%d -LocalServerReadyFile=\"%s\""),
        *ProjectFilePath,
        *MapName,
        Port,
        *GLocalDedicatedServerReadyFilePath
    );

    uint32 OutProcessID = 0;

    GLocalDedicatedServerHandle = FPlatformProcess::CreateProc(
        *ServerExePath,
        *Params,
        true,   // bLaunchDetached
        false,  // bLaunchHidden
        false,  // bLaunchReallyHidden
        &OutProcessID,
        0,
        nullptr,
        nullptr
    );

    if (!GLocalDedicatedServerHandle.IsValid())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[LocalDedicatedServer] 서버 프로세스 실행에 실패했습니다: %s %s"),
            *ServerExePath,
            *Params
        );

        // 서버 실행에 실패했으므로 Ready 상태도 폐기합니다.
        IFileManager::Get().Delete(
            *GLocalDedicatedServerReadyFilePath,
            false,
            true
        );

        GLocalDedicatedServerReadyFilePath.Reset();

        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[LocalDedicatedServer] 서버 프로세스를 시작했습니다. PID=%u, ReadyFile=%s"),
        OutProcessID,
        *GLocalDedicatedServerReadyFilePath
    );

    return true;
}

void ULocalDedicatedServerLibrary::StopLocalDedicatedServer()
{
    if (GLocalDedicatedServerHandle.IsValid())
    {
        FPlatformProcess::TerminateProc(
            GLocalDedicatedServerHandle,
            true
        );

        FPlatformProcess::CloseProc(
            GLocalDedicatedServerHandle
        );

        GLocalDedicatedServerHandle.Reset();
    }

    if (!GLocalDedicatedServerReadyFilePath.IsEmpty())
    {
        IFileManager::Get().Delete(
            *GLocalDedicatedServerReadyFilePath,
            false,
            true
        );

        GLocalDedicatedServerReadyFilePath.Reset();
    }
}

bool ULocalDedicatedServerLibrary::IsLocalDedicatedServerRunning()
{
	return GLocalDedicatedServerHandle.IsValid() && FPlatformProcess::IsProcRunning(GLocalDedicatedServerHandle);
}



bool ULocalDedicatedServerLibrary::IsLocalDedicatedServerReady()
{
    // Ready 파일이 있어도 서버 프로세스가 죽어 있다면 Ready가 아닙니다.
    if (!IsLocalDedicatedServerRunning())
    {
        return false;
    }

    if (GLocalDedicatedServerReadyFilePath.IsEmpty())
    {
        return false;
    }

    return FPaths::FileExists(
        GLocalDedicatedServerReadyFilePath
    );
}


bool ULocalDedicatedServerLibrary::MarkLocalDedicatedServerReady(
    UObject* WorldContextObject)
{
    if (!IsValid(WorldContextObject))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[LocalDedicatedServer] Ready 처리 실패: WorldContextObject가 유효하지 않습니다.")
        );

        return false;
    }

    UWorld* World = WorldContextObject->GetWorld();

    if (!IsValid(World))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[LocalDedicatedServer] Ready 처리 실패: World를 찾을 수 없습니다.")
        );

        return false;
    }

    // 실수로 클라이언트나 Standalone에서 호출되는 것을 방지합니다.
    if (World->GetNetMode() != NM_DedicatedServer)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[LocalDedicatedServer] Ready 처리는 Dedicated Server에서만 가능합니다.")
        );

        return false;
    }

    UNetDriver* NetDriver = World->GetNetDriver();

    if (!IsValid(NetDriver))
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[LocalDedicatedServer] 아직 NetDriver가 생성되지 않았습니다.")
        );

        return false;
    }

    if (!NetDriver->IsNetResourceValid())
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[LocalDedicatedServer] 아직 NetDriver의 네트워크 리소스가 준비되지 않았습니다.")
        );

        return false;
    }

    // 클라이언트 프로세스가 CreateProc() 실행 시 넘겨준 경로를 읽습니다.
    FString ReadyFilePath;

    if (!FParse::Value(
        FCommandLine::Get(),
        LocalServerReadyArgument,
        ReadyFilePath))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[LocalDedicatedServer] LocalServerReadyFile 실행 인자를 찾을 수 없습니다.")
        );

        return false;
    }

    if (ReadyFilePath.IsEmpty())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[LocalDedicatedServer] Ready 파일 경로가 비어 있습니다.")
        );

        return false;
    }

    const bool bSaved = FFileHelper::SaveStringToFile(
        TEXT("READY"),
        *ReadyFilePath
    );

    if (!bSaved)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[LocalDedicatedServer] Ready 파일 생성에 실패했습니다: %s"),
            *ReadyFilePath
        );

        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[LocalDedicatedServer] 서버 Ready 완료: %s"),
        *ReadyFilePath
    );

    return true;
}