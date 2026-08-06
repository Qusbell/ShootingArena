#include "AI/Area/AreaManagerSubsystem.h"

#include "AI/Area/AIAreaBase.h"
#include "AI/Area/AreaManagerActorBase.h"
#include "AI/Area/AreaRiskConfigDataAsset.h"
#include "AI/Area/Internal/AreaGraphService.h"
#include "AI/Area/Internal/AreaRiskService.h"
#include "AI/Area/Internal/AreaRouteFinder.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAreaSubsystem, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogAreaRiskDebug, Log, All);

namespace AreaRiskDebugConsole
{
    int32 bEnabled = 0;
    int32 bDumpOnce = 0;
    FString ObserverFilter;

    FAutoConsoleVariableRef CVarEnabled(
        TEXT("ai.AreaRisk.Debug"),
        bEnabled,
        TEXT(
            "AI별 Area 위험도 변경 로그를 켜거나 끕니다.\n"
            "0: 끔\n"
            "1: 켬 - 최초 전체 출력 후 변경된 값만 출력"),
        ECVF_Cheat);

    FAutoConsoleVariableRef CVarDumpOnce(
        TEXT("ai.AreaRisk.Dump"),
        bDumpOnce,
        TEXT(
            "현재 모든 AI x 모든 Area 위험도를 한 번 출력합니다.\n"
            "사용: ai.AreaRisk.Dump 1"),
        ECVF_Cheat);

    FAutoConsoleVariableRef CVarObserverFilter(
        TEXT("ai.AreaRisk.Filter"),
        ObserverFilter,
        TEXT(
            "Controller 이름에 포함되는 문자열만 출력합니다.\n"
            "빈 문자열 또는 None은 필터를 해제합니다."),
        ECVF_Cheat);
}

UAreaManagerSubsystem::UAreaManagerSubsystem() = default;

UAreaManagerSubsystem::~UAreaManagerSubsystem()
{
    // 내부 Service 타입이 완전히 정의된 cpp에서만 삭제합니다.
    DestroyServices();
}

void UAreaManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Hot Reload나 재초기화 상황에서도 기존 Service가 남지 않도록 먼저 정리합니다.
    DestroyServices();

    GraphService = new FAreaGraphService();
    RiskService = new FAreaRiskService(*GraphService);
    RouteFinder = new FAreaRouteFinder(GetWorld(), *GraphService, *RiskService);

    CachedRiskConfig = nullptr;
    CachedRetreatRoutes.Reset();
    bGraphReady = false;
}

void UAreaManagerSubsystem::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(AreaRiskDebugTimerHandle);
    }

    ResetAreaRiskDebugState();
    CachedRetreatRoutes.Reset();

    bGraphReady = false;
    CachedRiskConfig = nullptr;

    if (RiskService)
    {
        RiskService->Reset();
    }

    DestroyServices();

    Super::Deinitialize();
}

void UAreaManagerSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    RebuildRuntimeGraph();

    /*
     * 디버그가 꺼져 있을 때는 정수 CVar 몇 개만 확인하므로 비용이 매우 작습니다.
     * 실제 위험도 전체 순회는 Debug 또는 Dump가 요청됐을 때만 실행합니다.
     */
    if (InWorld.GetNetMode() != NM_Client)
    {
        InWorld.GetTimerManager().SetTimer(
            AreaRiskDebugTimerHandle,
            this,
            &UAreaManagerSubsystem::HandleAreaRiskDebugTimer,
            1.0f,
            true,
            1.0f);
    }
}

bool UAreaManagerSubsystem::RebuildRuntimeGraph()
{
    bGraphReady = false;
    CachedRiskConfig = nullptr;

    if (!GraphService || !RiskService)
    {
        return false;
    }

    AAreaManagerActorBase* ManagerActor = FindManagerActor();
    if (IsValid(ManagerActor))
    {
        CachedRiskConfig = ManagerActor->GetRiskConfig();
    }

    RiskService->SetRiskConfig(CachedRiskConfig);

    if (!IsValid(CachedRiskConfig))
    {
        UE_LOG(
            LogAreaSubsystem,
            Warning,
            TEXT("DA_Area_Risk가 지정되지 않았습니다. 위험도 점수는 0으로 계산됩니다."));
    }

    CachedRetreatRoutes.Reset();

    bool bBuilt = false;

    /*
     * 런타임에는 Auto Normal 생성, Nav 투영, GetPathLength를 실행하지 않습니다.
     * Area/Link/Nav 변경 후 에디터의 RebuildAreaGraph로 저장된 데이터만 읽습니다.
     */
    if (IsValid(ManagerActor) && !ManagerActor->GetBakedConnections().IsEmpty())
    {
        if (!ManagerActor->ShouldPreferBakedGraph())
        {
            UE_LOG(
                LogAreaSubsystem,
                Warning,
                TEXT("런타임 Nav 계산을 막기 위해 bPreferBakedGraph 설정과 관계없이 저장된 Area Graph를 사용합니다."));
        }

        bBuilt = GraphService->LoadBakedConnections(GetWorld(), ManagerActor);
    }
    else
    {
        UE_LOG(
            LogAreaSubsystem,
            Error,
            TEXT("저장된 Area Graph가 없습니다. 에디터에서 AreaManager의 RebuildAreaGraph를 실행하고 맵을 저장해야 합니다."));
    }

    bGraphReady = bBuilt;

    if (bGraphReady)
    {
        UE_LOG(
            LogAreaSubsystem,
            Log,
            TEXT("Area Runtime Graph 준비 완료: Area %d개, 방향성 연결 %d개, 에디터 Nav 캐시=%s"),
            GraphService->GetAreas().Num(),
            GraphService->GetConnections().Num(),
            GraphService->HasBakedRuntimeCaches() ? TEXT("Ready") : TEXT("Fallback"));

        if (!GraphService->HasBakedRuntimeCaches())
        {
            UE_LOG(
                LogAreaSubsystem,
                Warning,
                TEXT("Area Nav 캐시가 없거나 불완전합니다. 런타임 Nav 조회는 하지 않고 직선거리 Fallback을 사용합니다. RebuildAreaGraph 실행을 권장합니다."));
        }
    }
    else
    {
        UE_LOG(LogAreaSubsystem, Warning, TEXT("Area Runtime Graph를 구성하지 못했습니다."));
    }

    return bGraphReady;
}

AAIAreaBase* UAreaManagerSubsystem::GetAreaByPosition(
    const FVector& WorldPosition) const
{
    return GraphService
        ? GraphService->FindAreaByPosition(WorldPosition)
        : nullptr;
}

float UAreaManagerSubsystem::GetAreaRiskScore(
    AAIAreaBase* Area,
    AAIController* ObserverController) const
{
    return RiskService
        ? RiskService->GetAreaRiskScore(Area, ObserverController)
        : 0.0f;
}

FAreaRiskScoreResult UAreaManagerSubsystem::GetAreaRiskScoreDetails(
    AAIAreaBase* Area,
    AAIController* ObserverController) const
{
    return RiskService
        ? RiskService->GetAreaRiskScoreDetails(Area, ObserverController)
        : FAreaRiskScoreResult();
}

bool UAreaManagerSubsystem::SetAreaRecognized(
    AAIAreaBase* Area,
    AAIController* ObserverController,
    const bool bRecognized)
{
    if (!CanWriteAuthoritativeRisk() || !RiskService)
    {
        return false;
    }

    return RiskService->SetAreaRecognized(
        Area,
        ObserverController,
        bRecognized);
}

bool UAreaManagerSubsystem::SetAreaRecognizedByPosition(
    const FVector& AreaPosition,
    AAIController* ObserverController,
    const bool bRecognized)
{
    if (!GraphService)
    {
        return false;
    }

    AAIAreaBase* Area = GraphService->FindAreaByPosition(AreaPosition);
    return SetAreaRecognized(Area, ObserverController, bRecognized);
}

bool UAreaManagerSubsystem::RecognizeUnit(
    const FVector& UnitLocation,
    AActor* UnitActor,
    AAIController* ObserverController)
{
    if (!CanWriteAuthoritativeRisk() || !RiskService)
    {
        return false;
    }

    return RiskService->RecognizeUnit(
        UnitLocation,
        UnitActor,
        ObserverController);
}

bool UAreaManagerSubsystem::ForgetRecognizedUnit(
    AActor* UnitActor,
    AAIController* ObserverController)
{
    if (!CanWriteAuthoritativeRisk() || !RiskService)
    {
        return false;
    }

    return RiskService->ForgetRecognizedUnit(
        UnitActor,
        ObserverController);
}

bool UAreaManagerSubsystem::MarkCombatDetected(
    const FVector& EventLocation,
    AAIController* ObserverController)
{
    if (!CanWriteAuthoritativeRisk() || !RiskService)
    {
        return false;
    }

    return RiskService->MarkCombatDetected(
        EventLocation,
        ObserverController);
}

bool UAreaManagerSubsystem::ClearCombatDetected(
    AAIAreaBase* Area,
    AAIController* ObserverController)
{
    if (!CanWriteAuthoritativeRisk() || !RiskService)
    {
        return false;
    }

    return RiskService->ClearCombatDetected(
        Area,
        ObserverController);
}

bool UAreaManagerSubsystem::ClearCombatDetectedByPosition(
    const FVector& AreaPosition,
    AAIController* ObserverController)
{
    if (!GraphService)
    {
        return false;
    }

    AAIAreaBase* Area = GraphService->FindAreaByPosition(AreaPosition);
    return ClearCombatDetected(Area, ObserverController);
}

void UAreaManagerSubsystem::RemoveObserverRiskMemory(
    AAIController* ObserverController)
{
    ClearRetreatRouteCache(ObserverController);

    if (RiskService)
    {
        RiskService->RemoveObserverMemory(ObserverController);
    }
}

void UAreaManagerSubsystem::CleanupInvalidRiskMemory()
{
    if (RiskService)
    {
        RiskService->CleanupInvalidMemory();
    }
}

bool UAreaManagerSubsystem::FindSafestAreaRoute(
    const FAreaRouteRequest& Request,
    FAreaRouteResult& OutResult) const
{
    if (!bGraphReady || !RouteFinder)
    {
        OutResult.Reset();
        OutResult.FailureReason =
            FText::FromString(TEXT("Area Runtime Graph가 준비되지 않았습니다."));
        return false;
    }

    return RouteFinder->FindSafestRoute(Request, OutResult);
}

bool UAreaManagerSubsystem::FindSafestAreaRouteToActor(
    const FVector& StartPosition,
    AActor* TargetActor,
    AAIController* ObserverController,
    const FAreaTraversalCapabilities& TraversalCapabilities,
    FAreaRouteResult& OutResult) const
{
    if (!IsValid(TargetActor))
    {
        OutResult.Reset();
        OutResult.FailureReason =
            FText::FromString(TEXT("TargetActor가 유효하지 않습니다."));
        return false;
    }

    FAreaRouteRequest Request;
    Request.StartPosition = StartPosition;
    Request.TargetPosition = TargetActor->GetActorLocation();
    Request.ObserverController = ObserverController;
    Request.TraversalCapabilities = TraversalCapabilities;

    return FindSafestAreaRoute(Request, OutResult);
}

bool UAreaManagerSubsystem::FindAreaRoute(
    const FAreaRouteRequest& Request,
    FAreaRouteResult& OutResult) const
{
    if (!bGraphReady || !RouteFinder)
    {
        OutResult.Reset();
        OutResult.FailureReason =
            FText::FromString(TEXT("Area Runtime Graph가 준비되지 않았습니다."));
        return false;
    }

    // 위험도 Service를 호출하지 않고 거리만으로 경로를 비교합니다.
    return RouteFinder->FindRoute(Request, OutResult);
}

bool UAreaManagerSubsystem::FindAreaRouteToActor(
    const FVector& StartPosition,
    AActor* TargetActor,
    const FAreaTraversalCapabilities& TraversalCapabilities,
    FAreaRouteResult& OutResult) const
{
    if (!IsValid(TargetActor))
    {
        OutResult.Reset();
        OutResult.FailureReason =
            FText::FromString(TEXT("TargetActor가 유효하지 않습니다."));
        return false;
    }

    FAreaRouteRequest Request;
    Request.StartPosition = StartPosition;
    Request.TargetPosition = TargetActor->GetActorLocation();
    Request.TraversalCapabilities = TraversalCapabilities;

    return FindAreaRoute(Request, OutResult);
}

TArray<AAIAreaBase*> UAreaManagerSubsystem::GetRegisteredAreas() const
{
    TArray<AAIAreaBase*> Result;

    if (!GraphService)
    {
        return Result;
    }

    for (const TWeakObjectPtr<AAIAreaBase>& AreaPtr : GraphService->GetAreas())
    {
        if (AAIAreaBase* Area = AreaPtr.Get())
        {
            Result.Add(Area);
        }
    }

    return Result;
}

bool UAreaManagerSubsystem::GetBestBakedRetreatPoint(
    AAIAreaBase* Area,
    const FVector& FromPosition,
    FVector& OutPoint) const
{
    return GraphService
        ? GraphService->GetBestBakedRetreatPoint(Area, FromPosition, OutPoint)
        : false;
}

void UAreaManagerSubsystem::StoreRetreatRouteCache(
    AAIController* ObserverController,
    const FVector& StartPosition,
    const FAreaRouteResult& RouteResult)
{
    if (!IsValid(ObserverController) || !RouteResult.bSuccess)
    {
        ClearRetreatRouteCache(ObserverController);
        return;
    }

    const TWeakObjectPtr<AAIController> ObserverKey(ObserverController);
    FCachedAreaRetreatRoute& Cached = CachedRetreatRoutes.FindOrAdd(ObserverKey);
    Cached.StartArea = GetAreaByPosition(StartPosition);
    Cached.StartPosition = StartPosition;
    Cached.StoredWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    Cached.RouteResult = RouteResult;
}

bool UAreaManagerSubsystem::TryGetRetreatRouteCache(
    AAIController* ObserverController,
    const FVector& CurrentPosition,
    FAreaRouteResult& OutRouteResult) const
{
    OutRouteResult.Reset();

    if (!IsValid(ObserverController))
    {
        return false;
    }

    const TWeakObjectPtr<AAIController> ObserverKey(ObserverController);
    const FCachedAreaRetreatRoute* Cached = CachedRetreatRoutes.Find(ObserverKey);
    if (Cached == nullptr || !Cached->RouteResult.bSuccess)
    {
        return false;
    }

    AAIAreaBase* CurrentArea = GetAreaByPosition(CurrentPosition);
    if (!Cached->StartArea.IsValid() || Cached->StartArea.Get() != CurrentArea)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    const double CurrentWorldTime = World ? World->GetTimeSeconds() : 0.0;
    constexpr double MaxCacheAgeSeconds = 1.0;
    constexpr float MaxStartMovementDistance = 300.0f;

    if (CurrentWorldTime - Cached->StoredWorldTime > MaxCacheAgeSeconds
        || FVector::DistSquared(CurrentPosition, Cached->StartPosition)
            > FMath::Square(MaxStartMovementDistance))
    {
        return false;
    }

    OutRouteResult = Cached->RouteResult;
    return true;
}

void UAreaManagerSubsystem::ClearRetreatRouteCache(
    AAIController* ObserverController)
{
    if (ObserverController != nullptr)
    {
        CachedRetreatRoutes.Remove(TWeakObjectPtr<AAIController>(ObserverController));
    }
}

void UAreaManagerSubsystem::HandleAreaRiskDebugTimer()
{
    UWorld* World = GetWorld();
    if (!IsValid(World) || World->GetNetMode() == NM_Client)
    {
        return;
    }

    const bool bDebugEnabled =
        AreaRiskDebugConsole::bEnabled != 0;
    const bool bDumpRequested =
        AreaRiskDebugConsole::bDumpOnce != 0;

    if (bDumpRequested)
    {
        // 같은 요청이 매 타이머마다 반복되지 않도록 즉시 0으로 되돌립니다.
        AreaRiskDebugConsole::bDumpOnce = 0;
        DumpAreaRiskDebug(false);
    }

    if (bDebugEnabled)
    {
        if (!bWasAreaRiskDebugEnabled)
        {
            ResetAreaRiskDebugState();
            bWasAreaRiskDebugEnabled = true;

            UE_LOG(
                LogAreaRiskDebug,
                Log,
                TEXT(
                    "[AreaRiskDebug] 시작 | Filter=%s | "
                    "최초 전체 출력 후 변경된 값만 출력합니다."),
                AreaRiskDebugConsole::ObserverFilter.IsEmpty()
                    ? TEXT("<All>")
                    : *AreaRiskDebugConsole::ObserverFilter);
        }

        DumpAreaRiskDebug(true);
    }
    else if (bWasAreaRiskDebugEnabled)
    {
        UE_LOG(LogAreaRiskDebug, Log, TEXT("[AreaRiskDebug] 종료"));
        ResetAreaRiskDebugState();
    }
}

void UAreaManagerSubsystem::DumpAreaRiskDebug(
    const bool bOnlyChanged)
{
    UWorld* World = GetWorld();

    if (!IsValid(World))
    {
        return;
    }

    if (!bGraphReady)
    {
        UE_LOG(
            LogAreaRiskDebug,
            Warning,
            TEXT("[AreaRiskDebug] Area Runtime Graph가 준비되지 않았습니다."));
        return;
    }

    const TArray<AAIAreaBase*> Areas = GetRegisteredAreas();
    if (Areas.IsEmpty())
    {
        UE_LOG(
            LogAreaRiskDebug,
            Warning,
            TEXT("[AreaRiskDebug] 등록된 Area가 없습니다."));
        return;
    }

    int32 ObserverCount = 0;
    int32 OutputCount = 0;

    for (TActorIterator<AAIController> It(World); It; ++It)
    {
        AAIController* ObserverController = *It;

        if (!IsValid(ObserverController)
            || !PassesAreaRiskDebugFilter(ObserverController))
        {
            continue;
        }

        APawn* ControlledPawn = ObserverController->GetPawn();
        if (!IsValid(ControlledPawn))
        {
            continue;
        }

        ++ObserverCount;

        const FString ObserverName =
            GetAreaRiskDebugObserverName(ObserverController);
        const FString ObserverPath =
            ObserverController->GetPathName();

        AAIAreaBase* CurrentArea =
            GetAreaByPosition(ControlledPawn->GetActorLocation());

        const FName CurrentAreaId =
            IsValid(CurrentArea)
            ? CurrentArea->GetAreaId()
            : NAME_None;

        const FName* PreviousCurrentArea =
            LastAreaRiskDebugCurrentAreas.Find(ObserverPath);

        if (!bOnlyChanged
            || PreviousCurrentArea == nullptr
            || *PreviousCurrentArea != CurrentAreaId)
        {
            const FString PreviousAreaName =
                PreviousCurrentArea != nullptr
                ? PreviousCurrentArea->ToString()
                : TEXT("<Initial>");

            UE_LOG(
                LogAreaRiskDebug,
                Log,
                TEXT(
                    "[AreaRiskCurrentArea] Observer=%s | %s -> %s | "
                    "PawnLocation=%s"),
                *ObserverName,
                *PreviousAreaName,
                *GetAreaRiskDebugAreaName(CurrentArea),
                *ControlledPawn->GetActorLocation().ToCompactString());

            ++OutputCount;
        }

        LastAreaRiskDebugCurrentAreas.Add(
            ObserverPath,
            CurrentAreaId);

        for (AAIAreaBase* Area : Areas)
        {
            if (!IsValid(Area))
            {
                continue;
            }

            const FAreaRiskScoreResult Result =
                GetAreaRiskScoreDetails(
                    Area,
                    ObserverController);

            const FAreaRiskDebugSnapshot CurrentSnapshot =
                FAreaRiskDebugSnapshot::FromResult(Result);

            const FString SnapshotKey =
                ObserverPath
                + TEXT("|")
                + Area->GetPathName();

            const FAreaRiskDebugSnapshot* PreviousSnapshot =
                LastAreaRiskDebugSnapshots.Find(SnapshotKey);

            const bool bChanged =
                PreviousSnapshot == nullptr
                || !PreviousSnapshot->Equals(CurrentSnapshot);

            if (!bOnlyChanged || bChanged)
            {
                const TCHAR* Prefix = !bOnlyChanged
                    ? TEXT("[AreaRiskDump]")
                    : PreviousSnapshot == nullptr
                        ? TEXT("[AreaRiskInitial]")
                        : TEXT("[AreaRiskChanged]");

                UE_LOG(
                    LogAreaRiskDebug,
                    Log,
                    TEXT(
                        "%s Observer=%s | Area=%s | Current=%s | "
                        "Recognized=%s | Total=%d | Structural=%d | "
                        "Dynamic=%d | Units=%d | UnitScore=%d | "
                        "Combat=%s | CombatScore=%d | "
                        "Connections=%d | DeadEnd=%s"),
                    Prefix,
                    *ObserverName,
                    *GetAreaRiskDebugAreaName(Area),
                    Area == CurrentArea ? TEXT("true") : TEXT("false"),
                    Result.bIsAreaRecognized ? TEXT("true") : TEXT("false"),
                    Result.AreaRiskScore,
                    Result.StructuralRiskScore,
                    Result.DynamicRiskScore,
                    Result.RecognizedUnitCount,
                    Result.UnitCountScore,
                    Result.bIsCombatDetected ? TEXT("true") : TEXT("false"),
                    Result.CombatScore,
                    Result.ConnectedAreaCount,
                    Result.bIsDeadEnd ? TEXT("true") : TEXT("false"));

                if (PreviousSnapshot != nullptr)
                {
                    UE_LOG(
                        LogAreaRiskDebug,
                        Verbose,
                        TEXT(
                            "[AreaRiskPrevious] Observer=%s | Area=%s | "
                            "Recognized=%s | Total=%d | Structural=%d | "
                            "Dynamic=%d | Units=%d | Combat=%s"),
                        *ObserverName,
                        *GetAreaRiskDebugAreaName(Area),
                        PreviousSnapshot->bIsAreaRecognized
                            ? TEXT("true")
                            : TEXT("false"),
                        PreviousSnapshot->AreaRiskScore,
                        PreviousSnapshot->StructuralRiskScore,
                        PreviousSnapshot->DynamicRiskScore,
                        PreviousSnapshot->RecognizedUnitCount,
                        PreviousSnapshot->bIsCombatDetected
                            ? TEXT("true")
                            : TEXT("false"));
                }

                ++OutputCount;
            }

            LastAreaRiskDebugSnapshots.Add(
                SnapshotKey,
                CurrentSnapshot);
        }
    }

    if (ObserverCount == 0)
    {
        UE_LOG(
            LogAreaRiskDebug,
            Warning,
            TEXT(
                "[AreaRiskDebug] 조건에 맞는 유효한 AIController/Pawn이 없습니다. "
                "Filter=%s"),
            AreaRiskDebugConsole::ObserverFilter.IsEmpty()
                ? TEXT("<All>")
                : *AreaRiskDebugConsole::ObserverFilter);
    }
    else if (!bOnlyChanged)
    {
        UE_LOG(
            LogAreaRiskDebug,
            Log,
            TEXT(
                "[AreaRiskDumpComplete] Observer=%d | Area=%d | Output=%d"),
            ObserverCount,
            Areas.Num(),
            OutputCount);
    }
}

bool UAreaManagerSubsystem::PassesAreaRiskDebugFilter(
    const AAIController* ObserverController) const
{
    if (!IsValid(ObserverController))
    {
        return false;
    }

    const FString Filter =
        AreaRiskDebugConsole::ObserverFilter.TrimStartAndEnd();

    if (Filter.IsEmpty()
        || Filter.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        return true;
    }

    return ObserverController->GetName().Contains(
            Filter,
            ESearchCase::IgnoreCase)
        || ObserverController->GetPathName().Contains(
            Filter,
            ESearchCase::IgnoreCase);
}

FString UAreaManagerSubsystem::GetAreaRiskDebugObserverName(
    const AAIController* ObserverController)
{
    return IsValid(ObserverController)
        ? ObserverController->GetName()
        : TEXT("None");
}

FString UAreaManagerSubsystem::GetAreaRiskDebugAreaName(
    const AAIAreaBase* Area)
{
    if (!IsValid(Area))
    {
        return TEXT("None");
    }

    const FName AreaId = Area->GetAreaId();
    return AreaId.IsNone()
        ? Area->GetName()
        : AreaId.ToString();
}

void UAreaManagerSubsystem::ResetAreaRiskDebugState()
{
    LastAreaRiskDebugSnapshots.Reset();
    LastAreaRiskDebugCurrentAreas.Reset();
    bWasAreaRiskDebugEnabled = false;
}

bool UAreaManagerSubsystem::DoesSupportWorldType(
    const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game
        || WorldType == EWorldType::PIE
        || WorldType == EWorldType::Editor;
}

AAreaManagerActorBase* UAreaManagerSubsystem::FindManagerActor() const
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return nullptr;
    }

    AAreaManagerActorBase* FoundManager = nullptr;
    int32 ManagerCount = 0;

    for (TActorIterator<AAreaManagerActorBase> It(World); It; ++It)
    {
        if (IsValid(*It))
        {
            ++ManagerCount;

            if (FoundManager == nullptr)
            {
                FoundManager = *It;
            }
        }
    }

    if (ManagerCount > 1)
    {
        UE_LOG(
            LogAreaSubsystem,
            Warning,
            TEXT("AreaManagerActor가 %d개 배치되어 있습니다. 첫 번째 Actor를 사용합니다."),
            ManagerCount);
    }

    return FoundManager;
}

bool UAreaManagerSubsystem::CanWriteAuthoritativeRisk() const
{
    const UWorld* World = GetWorld();
    return IsValid(World) && World->GetNetMode() != NM_Client;
}

void UAreaManagerSubsystem::DestroyServices()
{
    // 의존 순서의 역순으로 삭제합니다.
    // RouteFinder는 GraphService와 RiskService를 참조하므로 가장 먼저 삭제해야 합니다.
    delete RouteFinder;
    RouteFinder = nullptr;

    delete RiskService;
    RiskService = nullptr;

    delete GraphService;
    GraphService = nullptr;
}
