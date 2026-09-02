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

	// 매치 서버(로비 서버가 스폰하는 별도 프로세스)용 핸들/Ready 파일 경로.
	// 위 캠페인용 상태와는 완전히 독립적으로 추적합니다.
	FProcHandle GMatchServerHandle;
	FString GMatchServerReadyFilePath;
}



bool ULocalDedicatedServerLibrary::StartLocalDedicatedServer(
    const FString& MapName,
    int32 Port)
{
    if (IsRunningDedicatedServer())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[LocalDedicatedServer] Dedicated Server 프로세스에서는 "
                "다른 로컬 Dedicated Server를 시작하지 않습니다.")
        );

        return false;
    }

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
    //
    // 별도의 Server 타겟(ShootingArenaServer.exe)을 쓰지 않고, 지금 이 코드를
    // 실행 중인 프로세스 자신의 실행 파일을 그대로 재사용합니다.
    // - 에디터에서 테스트 중이면 UnrealEditor.exe가 그대로 재실행되고 (-game -server로
    //   헤드리스 데디케이티드 서버가 됩니다).
    // - 패키징된(혹은 스탠드얼론으로 빌드된) ShootingArena.exe로 실행 중이면 그 실행 파일이
    //   그대로 재실행됩니다 (-server만으로 데디케이티드 서버가 됩니다).
    // Epic이 배포하는 Installed Build 엔진은 별도의 Server 타겟 빌드를 지원하지 않기 때문에
    // ("Server targets are not currently supported from this engine distribution."),
    // 이 방식으로 에디터 설치 여부와 무관하게 로컬 서버를 띄울 수 있습니다.
    // --------------------------------------------------------------------

    const FString ServerExePath = FPlatformProcess::ExecutablePath();

    if (ServerExePath.IsEmpty() || !FPaths::FileExists(ServerExePath))
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

#if WITH_EDITOR
    // UnrealEditor.exe는 어떤 프로젝트인지 모르므로 .uproject 경로를 첫 인자로 넘겨줘야 하고,
    // -game으로 게임 모드임을, -server로 데디케이티드 서버임을 명시해야 합니다.
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
#else
    // ShootingArena.exe는 이미 이 프로젝트 전용으로 빌드된 실행 파일이라 프로젝트 경로가 필요
    // 없고, -game 플래그도 의미가 없습니다. -server만 넘기면 헤드리스 데디케이티드 서버로 뜹니다.
    const FString Params = FString::Printf(
        TEXT("%s -server -log -port=%d -LocalServerReadyFile=\"%s\""),
        *MapName,
        Port,
        *GLocalDedicatedServerReadyFilePath
    );
#endif

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
        Warning,
        TEXT(
            "[LocalDedicatedServer] Spawned. "
            "CallerPID=%u / SpawnedPID=%u / IsDedicated=%d / ReadyFile=%s"
        ),
        FPlatformProcess::GetCurrentProcessId(),
        OutProcessID,
        IsRunningDedicatedServer() ? 1 : 0,
        * GLocalDedicatedServerReadyFilePath
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



// ============================================================================
// 매치 서버용 함수들. StartLocalDedicatedServer와 로직은 거의 동일하지만
// Dedicated Server 프로세스(로비 서버) 안에서 호출하는 것을 막지 않고,
// 별도의 핸들(GMatchServerHandle)로 추적합니다.
// Ready 확인/기록은 위 IsLocalDedicatedServerReady / MarkLocalDedicatedServerReady를
// 그대로 재사용합니다 (둘 다 "지금 프로세스가 어떤 Ready 파일을 쓰는지"만 보고,
// 캠페인인지 매치인지는 신경 쓰지 않는 범용 로직이기 때문입니다) — 단, IsMatchServerReady는
// GMatchServerHandle/GMatchServerReadyFilePath 기준으로 별도 구현합니다.
// ============================================================================

bool ULocalDedicatedServerLibrary::StartMatchServer(const FString& MapName, int32 Port)
{
    if (IsMatchServerRunning())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[MatchServer] 이미 실행 중인 매치 서버가 있어 새로 시작하지 않습니다.")
        );

        return false;
    }

    if (GMatchServerHandle.IsValid())
    {
        FPlatformProcess::CloseProc(GMatchServerHandle);
        GMatchServerHandle.Reset();
    }

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
            TEXT("[MatchServer] Ready 디렉터리를 생성하지 못했습니다: %s"),
            *ReadyDirectory
        );

        return false;
    }

    const FString ReadyFileName = FString::Printf(
        TEXT("MatchReady_%s.flag"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits)
    );

    GMatchServerReadyFilePath = FPaths::Combine(
        ReadyDirectory,
        ReadyFileName
    );

    IFileManager::Get().Delete(
        *GMatchServerReadyFilePath,
        false,
        true
    );

    // StartLocalDedicatedServer와 마찬가지로, 별도의 Server 타겟 대신 지금 이 프로세스 자신의
    // 실행 파일을 그대로 재사용합니다 (Installed Build 엔진은 Server 타겟 빌드를 지원하지 않습니다).
    const FString ServerExePath = FPlatformProcess::ExecutablePath();

    if (ServerExePath.IsEmpty() || !FPaths::FileExists(ServerExePath))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[MatchServer] 서버 실행 파일을 찾을 수 없습니다: %s"),
            *ServerExePath
        );

        GMatchServerReadyFilePath.Reset();
        return false;
    }

    // MapName에 "?Game=..." "?AIEasy=1" 같은 URL 옵션이 이미 이어붙어 있어도 그대로 통과시킵니다.
    // (StartLocalDedicatedServer와 달리 원래부터 -game 없이 -server만으로 잘 동작했으므로 그대로 유지합니다.)
#if WITH_EDITOR
    const FString ProjectFilePath =
        FPaths::ConvertRelativePathToFull(
            FPaths::GetProjectFilePath()
        );

    const FString Params = FString::Printf(
        TEXT("\"%s\" %s -server -log -port=%d -LocalServerReadyFile=\"%s\""),
        *ProjectFilePath,
        *MapName,
        Port,
        *GMatchServerReadyFilePath
    );
#else
    const FString Params = FString::Printf(
        TEXT("%s -server -log -port=%d -LocalServerReadyFile=\"%s\""),
        *MapName,
        Port,
        *GMatchServerReadyFilePath
    );
#endif

    uint32 OutProcessID = 0;

    GMatchServerHandle = FPlatformProcess::CreateProc(
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

    if (!GMatchServerHandle.IsValid())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[MatchServer] 서버 프로세스 실행에 실패했습니다: %s %s"),
            *ServerExePath,
            *Params
        );

        IFileManager::Get().Delete(
            *GMatchServerReadyFilePath,
            false,
            true
        );

        GMatchServerReadyFilePath.Reset();
        return false;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[MatchServer] Spawned. CallerPID=%u / SpawnedPID=%u / ReadyFile=%s"),
        FPlatformProcess::GetCurrentProcessId(),
        OutProcessID,
        *GMatchServerReadyFilePath
    );

    return true;
}

void ULocalDedicatedServerLibrary::StopMatchServer()
{
    if (GMatchServerHandle.IsValid())
    {
        FPlatformProcess::TerminateProc(
            GMatchServerHandle,
            true
        );

        FPlatformProcess::CloseProc(
            GMatchServerHandle
        );

        GMatchServerHandle.Reset();
    }

    if (!GMatchServerReadyFilePath.IsEmpty())
    {
        IFileManager::Get().Delete(
            *GMatchServerReadyFilePath,
            false,
            true
        );

        GMatchServerReadyFilePath.Reset();
    }
}

bool ULocalDedicatedServerLibrary::IsMatchServerRunning()
{
    return GMatchServerHandle.IsValid() && FPlatformProcess::IsProcRunning(GMatchServerHandle);
}

bool ULocalDedicatedServerLibrary::IsMatchServerReady()
{
    if (!IsMatchServerRunning())
    {
        return false;
    }

    if (GMatchServerReadyFilePath.IsEmpty())
    {
        return false;
    }

    return FPaths::FileExists(
        GMatchServerReadyFilePath
    );
}