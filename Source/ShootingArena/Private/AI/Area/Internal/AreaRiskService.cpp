#include "AI/Area/Internal/AreaRiskService.h"

#include "AI/Area/AIAreaBase.h"
#include "AI/Area/AreaRiskConfigDataAsset.h"
#include "AI/Area/Internal/AreaGraphService.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

FAreaRiskService::FAreaRiskService(FAreaGraphService& InGraphService)
    : GraphService(InGraphService)
{
}

void FAreaRiskService::SetRiskConfig(UAreaRiskConfigDataAsset* InRiskConfig)
{
    RiskConfig = InRiskConfig;
}

bool FAreaRiskService::SetAreaRecognized(
    AAIAreaBase* Area,
    AAIController* ObserverController,
    const bool bRecognized)
{
    if (!IsValid(Area) || !IsValid(ObserverController))
    {
        return false;
    }

    CleanupInvalidMemory();

    if (bRecognized)
    {
        FAreaObserverRiskMemory& Memory = FindOrAddObserverMemory(ObserverController);
        AddAreaUnique(Memory.RecognizedAreas, Area);
        return true;
    }

    FAreaObserverRiskMemory* Memory = FindObserverMemory(ObserverController);
    if (Memory == nullptr)
    {
        return true;
    }

    // AI 기억에서 Area 자체를 해제한 경우에는 해당 Area의 동적 기억도 즉시 제거합니다.
    RemoveArea(Memory->RecognizedAreas, Area);

    Memory->CombatMemories.RemoveAll([Area](const FAreaCombatMemoryRuntime& Combat)
    {
        return Combat.Area.Get() == Area;
    });

    Memory->RecognizedUnits.RemoveAll([Area](const FAreaRecognizedUnitRuntime& Unit)
    {
        return Unit.Area.Get() == Area;
    });

    return true;
}

bool FAreaRiskService::RecognizeUnit(
    const FVector& UnitLocation,
    AActor* UnitActor,
    AAIController* ObserverController)
{
    if (!IsValid(UnitActor) || !IsValid(ObserverController))
    {
        return false;
    }

    AAIAreaBase* UnitArea = GraphService.FindAreaByPosition(UnitLocation);
    if (!IsValid(UnitArea))
    {
        return false;
    }

    CleanupInvalidMemory();

    FAreaObserverRiskMemory& Memory = FindOrAddObserverMemory(ObserverController);
    const double CurrentTime = GetCurrentWorldTime(ObserverController, UnitArea);

    // 시야로 유닛을 확인했다면 해당 유닛이 있는 Area도 현재 인식한 구역으로 처리합니다.
    AddAreaUnique(Memory.RecognizedAreas, UnitArea);

    // 같은 유닛은 중복 추가하지 않고 위치와 마지막 확인 시간만 갱신합니다.
    for (FAreaRecognizedUnitRuntime& Unit : Memory.RecognizedUnits)
    {
        if (Unit.UnitActor.Get() == UnitActor)
        {
            Unit.Area = UnitArea;
            Unit.LastObservedTime = CurrentTime;
            return true;
        }
    }

    FAreaRecognizedUnitRuntime NewUnit;
    NewUnit.UnitActor = UnitActor;
    NewUnit.Area = UnitArea;
    NewUnit.LastObservedTime = CurrentTime;
    Memory.RecognizedUnits.Add(NewUnit);
    return true;
}

bool FAreaRiskService::ForgetRecognizedUnit(
    AActor* UnitActor,
    AAIController* ObserverController)
{
    if (!IsValid(ObserverController) || UnitActor == nullptr)
    {
        return false;
    }

    FAreaObserverRiskMemory* Memory = FindObserverMemory(ObserverController);
    if (Memory == nullptr)
    {
        return true;
    }

    Memory->RecognizedUnits.RemoveAll([UnitActor](const FAreaRecognizedUnitRuntime& Unit)
    {
        return Unit.UnitActor.Get() == UnitActor;
    });

    return true;
}

bool FAreaRiskService::MarkCombatDetected(
    const FVector& EventLocation,
    AAIController* ObserverController)
{
    if (!IsValid(ObserverController))
    {
        return false;
    }

    AAIAreaBase* EventArea = GraphService.FindAreaByPosition(EventLocation);
    if (!IsValid(EventArea))
    {
        return false;
    }

    CleanupInvalidMemory();

    FAreaObserverRiskMemory& Memory = FindOrAddObserverMemory(ObserverController);
    const double CurrentTime = GetCurrentWorldTime(ObserverController, EventArea);

    // 사격 또는 피격을 확인했다면 그 위치의 Area는 해당 AI가 인식한 Area입니다.
    AddAreaUnique(Memory.RecognizedAreas, EventArea);

    // 같은 Area의 교전은 점수를 누적하지 않고 마지막 확인 시간만 갱신합니다.
    for (FAreaCombatMemoryRuntime& Combat : Memory.CombatMemories)
    {
        if (Combat.Area.Get() == EventArea)
        {
            Combat.LastObservedTime = CurrentTime;
            return true;
        }
    }

    FAreaCombatMemoryRuntime NewCombat;
    NewCombat.Area = EventArea;
    NewCombat.LastObservedTime = CurrentTime;
    Memory.CombatMemories.Add(NewCombat);
    return true;
}

bool FAreaRiskService::ClearCombatDetected(
    AAIAreaBase* Area,
    AAIController* ObserverController)
{
    if (!IsValid(Area) || !IsValid(ObserverController))
    {
        return false;
    }

    FAreaObserverRiskMemory* Memory = FindObserverMemory(ObserverController);
    if (Memory == nullptr)
    {
        return true;
    }

    Memory->CombatMemories.RemoveAll([Area](const FAreaCombatMemoryRuntime& Combat)
    {
        return Combat.Area.Get() == Area;
    });

    return true;
}

FAreaRiskScoreResult FAreaRiskService::GetAreaRiskScoreDetails(
    const AAIAreaBase* Area,
    const AAIController* ObserverController) const
{
    FAreaRiskScoreResult Result;

    if (!IsValid(Area))
    {
        return Result;
    }

    Result.ConnectedAreaCount = CountConnectedAreas(Area);
    Result.bIsDeadEnd = Result.ConnectedAreaCount <= 1;

    const UAreaRiskConfigDataAsset* Config = RiskConfig.Get();
    if (IsValid(Config) && Result.bIsDeadEnd)
    {
        Result.StructuralRiskScore = FMath::Max(0, Config->DeadEndScore);
    }

    const FAreaObserverRiskMemory* Memory = FindObserverMemory(ObserverController);
    const double CurrentTime = GetCurrentWorldTime(ObserverController, Area);

    Result.bIsAreaRecognized =
        Memory != nullptr
        && ContainsArea(Memory->RecognizedAreas, Area);

    // 동적 위험도는 AI가 인식한 Area이며, 아직 만료되지 않은 기억만 사용합니다.
    if (Result.bIsAreaRecognized)
    {
        for (const FAreaRecognizedUnitRuntime& Unit : Memory->RecognizedUnits)
        {
            if (Unit.Area.Get() == Area && IsUnitMemoryActive(Unit, CurrentTime))
            {
                ++Result.RecognizedUnitCount;
            }
        }

        for (const FAreaCombatMemoryRuntime& Combat : Memory->CombatMemories)
        {
            if (Combat.Area.Get() == Area && IsCombatMemoryActive(Combat, CurrentTime))
            {
                Result.bIsCombatDetected = true;
                break;
            }
        }

        if (IsValid(Config))
        {
            const int64 UnitScore =
                static_cast<int64>(Result.RecognizedUnitCount)
                * static_cast<int64>(FMath::Max(0, Config->UnitCountWeight));

            Result.UnitCountScore = static_cast<int32>(
                FMath::Clamp<int64>(UnitScore, 0, MAX_int32));

            Result.CombatScore = Result.bIsCombatDetected
                ? FMath::Max(0, Config->CombatOccurrenceScore)
                : 0;
        }
    }

    const int64 DynamicScore =
        static_cast<int64>(Result.UnitCountScore)
        + static_cast<int64>(Result.CombatScore);

    Result.DynamicRiskScore = static_cast<int32>(
        FMath::Clamp<int64>(DynamicScore, 0, MAX_int32));

    const int64 TotalScore =
        static_cast<int64>(Result.StructuralRiskScore)
        + static_cast<int64>(Result.DynamicRiskScore);

    Result.AreaRiskScore = static_cast<int32>(
        FMath::Clamp<int64>(TotalScore, 0, MAX_int32));

    return Result;
}

float FAreaRiskService::GetAreaRiskScore(
    const AAIAreaBase* Area,
    const AAIController* ObserverController) const
{
    return static_cast<float>(
        GetAreaRiskScoreDetails(Area, ObserverController).AreaRiskScore);
}

void FAreaRiskService::RemoveObserverMemory(const AAIController* ObserverController)
{
    ObserverMemories.RemoveAll([ObserverController](const FAreaObserverRiskMemory& Memory)
    {
        return Memory.ObserverController.Get() == ObserverController;
    });
}

void FAreaRiskService::CleanupInvalidMemory()
{
    ObserverMemories.RemoveAll([](const FAreaObserverRiskMemory& Memory)
    {
        return !Memory.ObserverController.IsValid();
    });

    for (FAreaObserverRiskMemory& Memory : ObserverMemories)
    {
        AAIController* ObserverController = Memory.ObserverController.Get();
        const double CurrentTime = GetCurrentWorldTime(ObserverController, nullptr);

        Memory.RecognizedAreas.RemoveAll([](const TWeakObjectPtr<AAIAreaBase>& Area)
        {
            return !Area.IsValid();
        });

        Memory.CombatMemories.RemoveAll([this, CurrentTime](const FAreaCombatMemoryRuntime& Combat)
        {
            return !Combat.Area.IsValid()
                || !IsCombatMemoryActive(Combat, CurrentTime);
        });

        Memory.RecognizedUnits.RemoveAll([this, CurrentTime](const FAreaRecognizedUnitRuntime& Unit)
        {
            return !IsUnitMemoryActive(Unit, CurrentTime);
        });
    }
}

void FAreaRiskService::Reset()
{
    ObserverMemories.Reset();
    RiskConfig = nullptr;
}

FAreaObserverRiskMemory* FAreaRiskService::FindObserverMemory(
    const AAIController* ObserverController)
{
    for (FAreaObserverRiskMemory& Memory : ObserverMemories)
    {
        if (Memory.ObserverController.Get() == ObserverController)
        {
            return &Memory;
        }
    }

    return nullptr;
}

const FAreaObserverRiskMemory* FAreaRiskService::FindObserverMemory(
    const AAIController* ObserverController) const
{
    for (const FAreaObserverRiskMemory& Memory : ObserverMemories)
    {
        if (Memory.ObserverController.Get() == ObserverController)
        {
            return &Memory;
        }
    }

    return nullptr;
}

FAreaObserverRiskMemory& FAreaRiskService::FindOrAddObserverMemory(
    AAIController* ObserverController)
{
    if (FAreaObserverRiskMemory* ExistingMemory = FindObserverMemory(ObserverController))
    {
        return *ExistingMemory;
    }

    FAreaObserverRiskMemory& NewMemory = ObserverMemories.AddDefaulted_GetRef();
    NewMemory.ObserverController = ObserverController;
    return NewMemory;
}

int32 FAreaRiskService::CountConnectedAreas(const AAIAreaBase* Area) const
{
    if (!IsValid(Area))
    {
        return 0;
    }

    TSet<const AAIAreaBase*> UniqueConnectedAreas;

    const TArray<int32>& ConnectionIndices =
        GraphService.GetOutgoingConnectionIndices(Area);

    const TArray<FAreaDirectedConnection>& Connections =
        GraphService.GetConnections();

    for (const int32 ConnectionIndex : ConnectionIndices)
    {
        if (!Connections.IsValidIndex(ConnectionIndex))
        {
            continue;
        }

        const FAreaDirectedConnection& Connection = Connections[ConnectionIndex];
        AAIAreaBase* ToArea = Connection.ToArea.Get();

        if (Connection.bEnabled && IsValid(ToArea) && ToArea != Area)
        {
            UniqueConnectedAreas.Add(ToArea);
        }
    }

    return UniqueConnectedAreas.Num();
}

double FAreaRiskService::GetCurrentWorldTime(
    const AAIController* ObserverController,
    const AAIAreaBase* Area)
{
    if (IsValid(ObserverController))
    {
        if (const UWorld* World = ObserverController->GetWorld())
        {
            return static_cast<double>(World->GetTimeSeconds());
        }
    }

    if (IsValid(Area))
    {
        if (const UWorld* World = Area->GetWorld())
        {
            return static_cast<double>(World->GetTimeSeconds());
        }
    }

    return 0.0;
}

bool FAreaRiskService::IsUnitMemoryActive(
    const FAreaRecognizedUnitRuntime& UnitMemory,
    const double CurrentTime) const
{
    if (!UnitMemory.UnitActor.IsValid() || !UnitMemory.Area.IsValid())
    {
        return false;
    }

    const UAreaRiskConfigDataAsset* Config = RiskConfig.Get();
    if (!IsValid(Config))
    {
        return false;
    }

    const double Lifetime = FMath::Max(
        0.1,
        static_cast<double>(Config->UnitMemoryLifetime));

    return CurrentTime >= UnitMemory.LastObservedTime
        && CurrentTime - UnitMemory.LastObservedTime <= Lifetime;
}

bool FAreaRiskService::IsCombatMemoryActive(
    const FAreaCombatMemoryRuntime& CombatMemory,
    const double CurrentTime) const
{
    if (!CombatMemory.Area.IsValid())
    {
        return false;
    }

    const UAreaRiskConfigDataAsset* Config = RiskConfig.Get();
    if (!IsValid(Config))
    {
        return false;
    }

    const double Lifetime = FMath::Max(
        0.1,
        static_cast<double>(Config->CombatMemoryLifetime));

    return CurrentTime >= CombatMemory.LastObservedTime
        && CurrentTime - CombatMemory.LastObservedTime <= Lifetime;
}

bool FAreaRiskService::ContainsArea(
    const TArray<TWeakObjectPtr<AAIAreaBase>>& Areas,
    const AAIAreaBase* Area)
{
    for (const TWeakObjectPtr<AAIAreaBase>& AreaPtr : Areas)
    {
        if (AreaPtr.Get() == Area)
        {
            return true;
        }
    }

    return false;
}

void FAreaRiskService::AddAreaUnique(
    TArray<TWeakObjectPtr<AAIAreaBase>>& Areas,
    AAIAreaBase* Area)
{
    if (!IsValid(Area) || ContainsArea(Areas, Area))
    {
        return;
    }

    Areas.Add(Area);
}

void FAreaRiskService::RemoveArea(
    TArray<TWeakObjectPtr<AAIAreaBase>>& Areas,
    const AAIAreaBase* Area)
{
    Areas.RemoveAll([Area](const TWeakObjectPtr<AAIAreaBase>& AreaPtr)
    {
        return AreaPtr.Get() == Area;
    });
}
