#include "AI/Area/AreaManagerActorBase.h"

#include "AI/Area/AIAreaBase.h"
#include "AI/Area/AreaRiskConfigDataAsset.h"
#include "AI/Area/Internal/AreaGraphService.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogAreaManagerActor, Log, All);

namespace AreaManagerActorPrivate
{
    static FColor GetTraversalDebugColor(const EAreaTraversalType Type)
    {
        switch (Type)
        {
        case EAreaTraversalType::Normal:   return FColor::Green;
        case EAreaTraversalType::Teleport: return FColor::Purple;
        case EAreaTraversalType::JumpPad:  return FColor::Cyan;
        case EAreaTraversalType::Drop:     return FColor::Orange;
        case EAreaTraversalType::Jump:     return FColor::Yellow;
        case EAreaTraversalType::Door:     return FColor::Blue;
        default:                           return FColor::White;
        }
    }
}

AAreaManagerActorBase::AAreaManagerActorBase()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AAreaManagerActorBase::RebuildAreaGraph()
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        UE_LOG(LogAreaManagerActor, Error, TEXT("Area Graph Rebuild 실패: World가 없습니다."));
        return;
    }

    FAreaGraphService GraphService;
    if (!GraphService.BuildFromWorldActors(World, this))
    {
        UE_LOG(LogAreaManagerActor, Error, TEXT("Area Graph Rebuild 실패: 유효한 Area를 찾지 못했습니다."));
        return;
    }

#if WITH_EDITOR
    Modify();
#endif

    // Endpoint 자동 판정 실패처럼 그래프에서 제외된 Link 원인을 Rebuild 시점에도 바로 보여줍니다.
    TArray<FString> Issues;
    GraphService.Validate(Issues);

    if (!IsValid(RiskConfig))
    {
        Issues.Add(TEXT("AreaManager의 Area Risk Data가 비어 있습니다. DA_Area_Risk를 지정해야 위험도 점수가 계산됩니다."));
    }

    for (const FString& Issue : Issues)
    {
        UE_LOG(LogAreaManagerActor, Warning, TEXT("Area Graph Rebuild 경고: %s"), *Issue);
    }

    GraphService.ExportBakedConnections(BakedConnections);
    MarkPackageDirty();

    UE_LOG(
        LogAreaManagerActor,
        Log,
        TEXT(
            "Area Graph Rebuild 완료: 방향성 연결 %d개, 경고 %d개 "
            "[AutoNormal=%s, Nav검증=%s, MaxGap=%.1f, MaxHeight=%.1f, "
            "SampleSpacing=%.1f, DetourRatio=%.2f, ExtraDistance=%.1f]"),
        BakedConnections.Num(),
        Issues.Num(),
        bBuildAutomaticNormalLinks ? TEXT("true") : TEXT("false"),
        bValidateAutoLinksWithNavigation ? TEXT("true") : TEXT("false"),
        AutoNormalMaxGap,
        AutoNormalMaxHeightDifference,
        AutoNormalBoundarySampleSpacing,
        AutoNormalMaxPathDetourRatio,
        AutoNormalMaxPathExtraDistance);
}

void AAreaManagerActorBase::ValidateAreaGraph()
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        UE_LOG(LogAreaManagerActor, Error, TEXT("Area Graph Validate 실패: World가 없습니다."));
        return;
    }

    FAreaGraphService GraphService;
    GraphService.BuildFromWorldActors(World, this);

    TArray<FString> Issues;
    GraphService.Validate(Issues);

    if (!IsValid(RiskConfig))
    {
        Issues.Add(TEXT("AreaManager의 Area Risk Data가 비어 있습니다. DA_Area_Risk를 지정해야 합니다."));
    }

    if (Issues.IsEmpty())
    {
        UE_LOG(LogAreaManagerActor, Log, TEXT("Area Graph Validate 성공: 문제를 찾지 못했습니다."));
        return;
    }

    UE_LOG(LogAreaManagerActor, Warning, TEXT("Area Graph Validate: 문제 %d개"), Issues.Num());
    for (const FString& Issue : Issues)
    {
        UE_LOG(LogAreaManagerActor, Warning, TEXT("- %s"), *Issue);
    }
}

void AAreaManagerActorBase::ClearBakedAreaGraph()
{
#if WITH_EDITOR
    Modify();
#endif

    BakedConnections.Reset();
    MarkPackageDirty();
    UE_LOG(LogAreaManagerActor, Log, TEXT("저장된 Area Graph를 비웠습니다."));
}

void AAreaManagerActorBase::DrawAreaGraphDebug()
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    TArray<FAreaBakedConnection> ConnectionsToDraw = BakedConnections;

    if (ConnectionsToDraw.IsEmpty())
    {
        FAreaGraphService GraphService;
        GraphService.BuildFromWorldActors(World, this);
        GraphService.ExportBakedConnections(ConnectionsToDraw);
    }

    for (const FAreaBakedConnection& Connection : ConnectionsToDraw)
    {
        if (!IsValid(Connection.FromArea) || !IsValid(Connection.ToArea))
        {
            continue;
        }

        const FColor Color = AreaManagerActorPrivate::GetTraversalDebugColor(Connection.TraversalType);

        DrawDebugLine(
            World,
            Connection.EntryLocation,
            Connection.ExitLocation,
            Color,
            false,
            DebugDrawDuration,
            0,
            5.0f);

        DrawDebugSphere(World, Connection.EntryLocation, 25.0f, 8, Color, false, DebugDrawDuration);
        DrawDebugSphere(World, Connection.ExitLocation, 25.0f, 8, Color, false, DebugDrawDuration);
    }
}
