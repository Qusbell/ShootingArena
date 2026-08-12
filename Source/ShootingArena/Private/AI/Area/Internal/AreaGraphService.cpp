#include "AI/Area/Internal/AreaGraphService.h"

#include "AI/Area/AIAreaBase.h"
#include "AI/Area/AreaLinkBase.h"
#include "AI/Area/AreaManagerActorBase.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"

namespace AreaGraphServicePrivate
{
    /** 한 Area의 한쪽 둘레 면에 생성할 최소/최대 후보 개수입니다. */
    constexpr int32 AutoNormalMinSamplesPerSide = 3;
    constexpr int32 AutoNormalMaxSamplesPerSide = 33;

    /**
     * 각 경계 후보마다 반대쪽에서 가까운 후보를 몇 개씩 검사할지 정합니다.
     * 모든 후보가 최소 한 번 이상 검사에 참여하므로, 중앙 벽 쪽의 가까운 조합만
     * 검사하다가 옆문 후보를 놓치는 문제를 방지합니다.
     */
    constexpr int32 AutoNormalNearestPairsPerCandidate = 8;

    /** 경계 후보를 NavMesh로 투영할 때 사용할 탐색 범위입니다. */
    const FVector AutoNormalProjectionExtent(300.0f, 300.0f, 500.0f);

    /** NavMesh 투영 결과가 Area 경계에서 약간 벗어나도 허용할 거리입니다. */
    constexpr float AutoNormalAreaTolerance = 75.0f;

    /** 같은 위치로 투영된 후보를 중복 제거할 때 사용할 거리입니다. */
    constexpr float AutoNormalCandidateMergeDistance = 25.0f;

    static void AddCandidateUnique(TArray<FVector>& Candidates, const FVector& Candidate)
    {
        const float MergeDistanceSquared =
            FMath::Square(AutoNormalCandidateMergeDistance);

        for (const FVector& Existing : Candidates)
        {
            if (FVector::DistSquared(Existing, Candidate) <= MergeDistanceSquared)
            {
                return;
            }
        }

        Candidates.Add(Candidate);
    }

    /**
     * 실제 월드 Actor를 반드시 Goal Actor로 전달해야 하는 이동 종류인지 반환합니다.
     *
     * Jump와 Drop은 실행 Actor가 없어도 Link의 Exit 위치를 목표로 사용할 수 있으므로
     * Actor를 필수로 요구하지 않습니다.
     */
    static bool RequiresTraversalActor(const EAreaTraversalType TraversalType)
    {
        return TraversalType == EAreaTraversalType::Teleport
            || TraversalType == EAreaTraversalType::JumpPad
            || TraversalType == EAreaTraversalType::Door;
    }

    static bool IsConnectionDataValid(const FAreaDirectedConnection& Connection)
    {
        const bool bHasRequiredTraversalActor =
            !RequiresTraversalActor(Connection.TraversalType)
            || Connection.TraversalActor.IsValid();

        return Connection.bEnabled
            && Connection.FromArea.IsValid()
            && Connection.ToArea.IsValid()
            && Connection.FromArea.Get() != Connection.ToArea.Get()
            && bHasRequiredTraversalActor;
    }
}

bool FAreaGraphService::BuildFromWorldActors(UWorld* World, const AAreaManagerActorBase* ManagerActor)
{
    Reset();

    if (IsValid(ManagerActor))
    {
        NearestAreaFallbackMaxDistance =
            FMath::Max(0.0f, ManagerActor->GetNearestAreaFallbackMaxDistance());
        NearestAreaDistanceTieTolerance =
            FMath::Max(0.0f, ManagerActor->GetNearestAreaDistanceTieTolerance());
    }

    if (!IsValid(World))
    {
        return false;
    }

    CollectAreas(World);

    TSet<uint32> ManualPairHashes;
    AddManualLinks(World, ManualPairHashes);

    if (ManagerActor == nullptr || ManagerActor->ShouldBuildAutomaticNormalLinks())
    {
        AddAutomaticNormalLinks(World, ManagerActor, ManualPairHashes);
    }

    RebuildAdjacency();
    return Areas.Num() > 0;
}

bool FAreaGraphService::LoadBakedConnections(UWorld* World, const AAreaManagerActorBase* ManagerActor)
{
    Reset();

    if (!IsValid(World) || !IsValid(ManagerActor))
    {
        return false;
    }

    NearestAreaFallbackMaxDistance =
        FMath::Max(0.0f, ManagerActor->GetNearestAreaFallbackMaxDistance());
    NearestAreaDistanceTieTolerance =
        FMath::Max(0.0f, ManagerActor->GetNearestAreaDistanceTieTolerance());

    CollectAreas(World);

    const TArray<FAreaBakedConnection>& BakedConnections = ManagerActor->GetBakedConnections();
    for (int32 BakedIndex = 0; BakedIndex < BakedConnections.Num(); ++BakedIndex)
    {
        const FAreaBakedConnection& Baked = BakedConnections[BakedIndex];
        FAreaDirectedConnection RuntimeConnection;
        RuntimeConnection.FromArea = Baked.FromArea;
        RuntimeConnection.ToArea = Baked.ToArea;
        RuntimeConnection.SourceLink = Baked.SourceLink;
        RuntimeConnection.TraversalActor = Baked.TraversalActor;
        RuntimeConnection.TraversalType = Baked.TraversalType;
        RuntimeConnection.EntryLocation = Baked.EntryLocation;
        RuntimeConnection.ExitLocation = Baked.ExitLocation;
        RuntimeConnection.TraversalDistanceCost = Baked.TraversalDistanceCost;
        RuntimeConnection.TraversalRiskCost = Baked.TraversalRiskCost;
        RuntimeConnection.bEnabled = Baked.bEnabled;
        RuntimeConnection.bAutomatic = Baked.bAutomatic;
        RuntimeConnection.BakedConnectionIndex = BakedIndex;

        if (AreaGraphServicePrivate::IsConnectionDataValid(RuntimeConnection))
        {
            AddDirectedConnection(RuntimeConnection);
        }
    }

    RebuildAdjacency();

    for (const FAreaBakedTransitionDistance& Cached : ManagerActor->GetBakedTransitionDistances())
    {
        TransitionDistanceCache.Add(
            MakeTransitionCacheKey(Cached.PreviousConnectionIndex, Cached.NextConnectionIndex),
            Cached);
    }

    for (const FAreaBakedRetreatPoint& CachedPoint : ManagerActor->GetBakedRetreatPoints())
    {
        if (IsValid(CachedPoint.Area))
        {
            RetreatPointsByArea.FindOrAdd(CachedPoint.Area).Add(CachedPoint.Location);
        }
    }

    bHasBakedRuntimeCaches = !TransitionDistanceCache.IsEmpty() && !RetreatPointsByArea.IsEmpty();
    return Areas.Num() > 0 && Connections.Num() > 0;
}

void FAreaGraphService::ExportBakedConnections(TArray<FAreaBakedConnection>& OutConnections) const
{
    OutConnections.Reset();
    OutConnections.Reserve(Connections.Num());

    for (const FAreaDirectedConnection& Connection : Connections)
    {
        if (!AreaGraphServicePrivate::IsConnectionDataValid(Connection))
        {
            continue;
        }

        FAreaBakedConnection Baked;
        Baked.FromArea = Connection.FromArea.Get();
        Baked.ToArea = Connection.ToArea.Get();
        Baked.SourceLink = Connection.SourceLink.Get();
        Baked.TraversalActor = Connection.TraversalActor.Get();
        Baked.TraversalType = Connection.TraversalType;
        Baked.EntryLocation = Connection.EntryLocation;
        Baked.ExitLocation = Connection.ExitLocation;
        Baked.TraversalDistanceCost = Connection.TraversalDistanceCost;
        Baked.TraversalRiskCost = Connection.TraversalRiskCost;
        Baked.bEnabled = Connection.bEnabled;
        Baked.bAutomatic = Connection.bAutomatic;
        OutConnections.Add(Baked);
    }
}

void FAreaGraphService::BuildEditorNavigationCaches(
    UWorld* World,
    const float RetreatPointInset,
    TArray<FAreaBakedTransitionDistance>& OutTransitionDistances,
    TArray<FAreaBakedRetreatPoint>& OutRetreatPoints) const
{
    OutTransitionDistances.Reset();
    OutRetreatPoints.Reset();

    if (!IsValid(World))
    {
        return;
    }

    // 이전 연결의 Exit에서 같은 Area의 다음 연결 목표까지의 고정 구간을 에디터에서 한 번만 계산합니다.
    for (int32 PreviousIndex = 0; PreviousIndex < Connections.Num(); ++PreviousIndex)
    {
        const FAreaDirectedConnection& Previous = Connections[PreviousIndex];
        AAIAreaBase* ArrivalArea = Previous.ToArea.Get();
        if (!IsValid(ArrivalArea))
        {
            continue;
        }

        const TArray<int32>& NextIndices = GetOutgoingConnectionIndices(ArrivalArea);
        for (const int32 NextIndex : NextIndices)
        {
            if (!Connections.IsValidIndex(NextIndex))
            {
                continue;
            }

            const FAreaDirectedConnection& Next = Connections[NextIndex];
            const FVector WalkTarget = Next.TraversalType == EAreaTraversalType::Normal
                ? Next.ExitLocation
                : Next.EntryLocation;

            FAreaBakedTransitionDistance& Cached = OutTransitionDistances.AddDefaulted_GetRef();
            Cached.PreviousConnectionIndex = PreviousIndex;
            Cached.NextConnectionIndex = NextIndex;

            double PathLength = 0.0;
            Cached.bReachable = TryGetNavigationPathLength(
                World,
                Previous.ExitLocation,
                WalkTarget,
                PathLength);
            Cached.NavDistance = Cached.bReachable
                ? static_cast<float>(FMath::Max(0.0, PathLength))
                : 0.0f;
        }
    }

    // Area 중심과 연결 Endpoint를 Area 안쪽으로 보정한 뒤 NavMesh에 투영해 여러 후퇴 후보를 저장합니다.
    const float SafeInset = FMath::Max(0.0f, RetreatPointInset);
    constexpr float MergeDistance = 50.0f;
    const float MergeDistanceSquared = FMath::Square(MergeDistance);

    for (const TWeakObjectPtr<AAIAreaBase>& AreaPtr : Areas)
    {
        AAIAreaBase* Area = AreaPtr.Get();
        if (!IsValid(Area))
        {
            continue;
        }

        TArray<FVector> RawCandidates;
        RawCandidates.Add(Area->GetAreaCenter());

        for (const FAreaDirectedConnection& Connection : Connections)
        {
            if (Connection.FromArea.Get() == Area)
            {
                RawCandidates.Add(Area->GetClosestPointInArea(Connection.EntryLocation, SafeInset));
            }

            if (Connection.ToArea.Get() == Area)
            {
                RawCandidates.Add(Area->GetClosestPointInArea(Connection.ExitLocation, SafeInset));
            }
        }

        TArray<FVector> AddedPoints;
        for (const FVector& RawCandidate : RawCandidates)
        {
            FVector ProjectedPoint;
            if (!TryProjectCandidateToNavigation(World, Area, RawCandidate, ProjectedPoint))
            {
                continue;
            }

            bool bDuplicate = false;
            for (const FVector& Existing : AddedPoints)
            {
                if (FVector::DistSquared(Existing, ProjectedPoint) <= MergeDistanceSquared)
                {
                    bDuplicate = true;
                    break;
                }
            }

            if (bDuplicate)
            {
                continue;
            }

            AddedPoints.Add(ProjectedPoint);

            FAreaBakedRetreatPoint& CachedPoint = OutRetreatPoints.AddDefaulted_GetRef();
            CachedPoint.Area = Area;
            CachedPoint.Location = ProjectedPoint;
        }
    }
}

bool FAreaGraphService::TryGetBakedTransitionDistance(
    const int32 PreviousConnectionIndex,
    const int32 NextConnectionIndex,
    float& OutDistance,
    bool& OutReachable) const
{
    OutDistance = 0.0f;
    OutReachable = false;

    const FAreaBakedTransitionDistance* Cached = TransitionDistanceCache.Find(
        MakeTransitionCacheKey(PreviousConnectionIndex, NextConnectionIndex));
    if (Cached == nullptr)
    {
        return false;
    }

    OutDistance = FMath::Max(0.0f, Cached->NavDistance);
    OutReachable = Cached->bReachable;
    return true;
}

bool FAreaGraphService::GetBestBakedRetreatPoint(
    const AAIAreaBase* Area,
    const FVector& FromPosition,
    FVector& OutPoint) const
{
    OutPoint = FVector::ZeroVector;

    const TArray<FVector>* Points = RetreatPointsByArea.Find(Area);
    if (Points == nullptr || Points->IsEmpty())
    {
        return false;
    }

    double BestDistanceSquared = TNumericLimits<double>::Max();
    bool bFound = false;

    for (const FVector& Point : *Points)
    {
        const double DistanceSquared = FVector::DistSquared(FromPosition, Point);
        if (!bFound || DistanceSquared < BestDistanceSquared)
        {
            bFound = true;
            BestDistanceSquared = DistanceSquared;
            OutPoint = Point;
        }
    }

    return bFound;
}

uint64 FAreaGraphService::MakeTransitionCacheKey(
    const int32 PreviousConnectionIndex,
    const int32 NextConnectionIndex)
{
    return (static_cast<uint64>(static_cast<uint32>(PreviousConnectionIndex)) << 32)
        | static_cast<uint64>(static_cast<uint32>(NextConnectionIndex));
}

void FAreaGraphService::Reset()
{
    Areas.Reset();
    Connections.Reset();
    OutgoingConnections.Reset();
    EmptyConnectionIndices.Reset();
    BuildIssues.Reset();
    TransitionDistanceCache.Reset();
    RetreatPointsByArea.Reset();
    bHasBakedRuntimeCaches = false;
    NearestAreaFallbackMaxDistance = 0.0f;
    NearestAreaDistanceTieTolerance = 1.0f;
}

AAIAreaBase* FAreaGraphService::FindAreaByPosition(const FVector& WorldPosition) const
{
    // 실제 Area 내부라면 기존 판정을 그대로 우선합니다.
    if (AAIAreaBase* ContainingArea = FindContainingAreaByPosition(WorldPosition))
    {
        return ContainingArea;
    }

    // Area 사이의 작은 빈 공간만 보완합니다. 맵 밖처럼 너무 먼 위치는 기존처럼 None입니다.
    return FindAreaByPositionOrNearest(
        WorldPosition,
        NearestAreaFallbackMaxDistance);
}

AAIAreaBase* FAreaGraphService::FindContainingAreaByPosition(const FVector& WorldPosition) const
{
    AAIAreaBase* BestArea = nullptr;

    for (const TWeakObjectPtr<AAIAreaBase>& AreaPtr : Areas)
    {
        AAIAreaBase* Area = AreaPtr.Get();
        if (!IsValid(Area) || !Area->ContainsPosition(WorldPosition))
        {
            continue;
        }

        // 겹친 Area는 더 작은 Area를 우선하고, 완전 동률이면 AreaId/Path로 고정합니다.
        if (!IsValid(BestArea) || IsAreaPreferredOnTie(Area, BestArea))
        {
            BestArea = Area;
        }
    }

    return BestArea;
}

AAIAreaBase* FAreaGraphService::FindAreaByPositionOrNearest(
    const FVector& WorldPosition,
    const float MaxDistance) const
{
    // Link Endpoint 판정에서는 일반 nearest fallback 최대 거리 대신 이 함수의 MaxDistance를 반드시 따릅니다.
    if (AAIAreaBase* ContainingArea = FindContainingAreaByPosition(WorldPosition))
    {
        return ContainingArea;
    }

    const double SafeMaxDistance = FMath::Max(0.0, static_cast<double>(MaxDistance));
    if (SafeMaxDistance <= 0.0)
    {
        return nullptr;
    }

    const double TieTolerance =
        FMath::Max(0.0, static_cast<double>(NearestAreaDistanceTieTolerance));

    AAIAreaBase* BestArea = nullptr;
    double BestDistance = SafeMaxDistance;

    for (const TWeakObjectPtr<AAIAreaBase>& AreaPtr : Areas)
    {
        AAIAreaBase* Area = AreaPtr.Get();
        if (!IsValid(Area) || !Area->IsAreaEnabled())
        {
            continue;
        }

        // Actor 중심이 아니라 실제 회전 Box 경계까지의 거리를 사용합니다.
        const FVector ClosestPoint = Area->GetClosestPointInArea(WorldPosition, 0.0f);
        const double Distance = FVector::Distance(WorldPosition, ClosestPoint);

        if (Distance > SafeMaxDistance)
        {
            continue;
        }

        if (!IsValid(BestArea) || Distance < BestDistance - TieTolerance)
        {
            BestArea = Area;
            BestDistance = Distance;
            continue;
        }

        if (FMath::Abs(Distance - BestDistance) <= TieTolerance)
        {
            const bool bPreferCandidate = IsAreaPreferredOnTie(Area, BestArea);
            BestDistance = FMath::Min(BestDistance, Distance);

            if (bPreferCandidate)
            {
                BestArea = Area;
            }
        }
    }

    return BestArea;
}

bool FAreaGraphService::IsAreaPreferredOnTie(
    const AAIAreaBase* Candidate,
    const AAIAreaBase* CurrentBest)
{
    if (!IsValid(Candidate))
    {
        return false;
    }

    if (!IsValid(CurrentBest))
    {
        return true;
    }

    const FVector CandidateExtent = Candidate->GetAreaExtent();
    const FVector BestExtent = CurrentBest->GetAreaExtent();

    const double CandidateVolume =
        static_cast<double>(CandidateExtent.X)
        * static_cast<double>(CandidateExtent.Y)
        * static_cast<double>(CandidateExtent.Z)
        * 8.0;

    const double BestVolume =
        static_cast<double>(BestExtent.X)
        * static_cast<double>(BestExtent.Y)
        * static_cast<double>(BestExtent.Z)
        * 8.0;

    const double VolumeTolerance =
        FMath::Max(1.0, FMath::Max(CandidateVolume, BestVolume) * 1.0e-6);

    if (CandidateVolume < BestVolume - VolumeTolerance)
    {
        return true;
    }

    if (CandidateVolume > BestVolume + VolumeTolerance)
    {
        return false;
    }

    const FString CandidateAreaId = Candidate->GetAreaId().ToString();
    const FString BestAreaId = CurrentBest->GetAreaId().ToString();
    const int32 AreaIdCompare = CandidateAreaId.Compare(BestAreaId, ESearchCase::IgnoreCase);

    if (AreaIdCompare != 0)
    {
        return AreaIdCompare < 0;
    }

    // 중복 AreaId까지 존재하더라도 Actor Path로 마지막 동률을 고정해 실행 순서 의존성을 없앱니다.
    return Candidate->GetPathName().Compare(
        CurrentBest->GetPathName(),
        ESearchCase::IgnoreCase) < 0;
}

const TArray<int32>& FAreaGraphService::GetOutgoingConnectionIndices(const AAIAreaBase* Area) const
{
    if (const TArray<int32>* Found = OutgoingConnections.Find(Area))
    {
        return *Found;
    }

    return EmptyConnectionIndices;
}

void FAreaGraphService::Validate(TArray<FString>& OutIssues) const
{
    OutIssues = BuildIssues;

    TMap<FName, int32> AreaIdCounts;
    for (const TWeakObjectPtr<AAIAreaBase>& AreaPtr : Areas)
    {
        const AAIAreaBase* Area = AreaPtr.Get();
        if (!IsValid(Area))
        {
            OutIssues.Add(TEXT("유효하지 않은 Area 참조가 그래프에 포함되어 있습니다."));
            continue;
        }

        if (Area->GetAreaId().IsNone())
        {
            OutIssues.Add(FString::Printf(TEXT("%s : AreaId가 None입니다."), *Area->GetName()));
        }
        else
        {
            AreaIdCounts.FindOrAdd(Area->GetAreaId())++;
        }
    }

    for (const TPair<FName, int32>& Pair : AreaIdCounts)
    {
        if (Pair.Value > 1)
        {
            OutIssues.Add(FString::Printf(TEXT("AreaId '%s'가 %d개 중복되었습니다."), *Pair.Key.ToString(), Pair.Value));
        }
    }

    for (int32 Index = 0; Index < Connections.Num(); ++Index)
    {
        const FAreaDirectedConnection& Connection = Connections[Index];
        if (!AreaGraphServicePrivate::IsConnectionDataValid(Connection))
        {
            OutIssues.Add(FString::Printf(TEXT("Connection[%d]가 유효하지 않습니다."), Index));
        }
    }
}

void FAreaGraphService::CollectAreas(UWorld* World)
{
    for (TActorIterator<AAIAreaBase> It(World); It; ++It)
    {
        AAIAreaBase* Area = *It;
        if (IsValid(Area) && Area->IsAreaEnabled())
        {
            Areas.Add(Area);
        }
    }
}

void FAreaGraphService::AddManualLinks(UWorld* World, TSet<uint32>& OutManualPairHashes)
{
    for (TActorIterator<AAreaLinkBase> It(World); It; ++It)
    {
        AAreaLinkBase* Link = *It;
        if (!IsValid(Link) || !Link->IsLinkEnabled())
        {
            continue;
        }

        FVector EntryLocation;
        FVector ExitLocation;
        FString ResolveFailureReason;

        if (!Link->TryResolveEndpointLocations(
            World,
            EntryLocation,
            ExitLocation,
            ResolveFailureReason))
        {
            BuildIssues.Add(FString::Printf(
                TEXT("%s : Endpoint 해석 실패 - %s"),
                *Link->GetName(),
                *ResolveFailureReason));
            continue;
        }

        // Area Override가 지정되어 있으면 우선 사용하고, 비어 있으면 Endpoint 위치로 자동 판정합니다.
        AAIAreaBase* AreaA = Link->GetAreaA();
        if (!IsValid(AreaA))
        {
            AreaA = FindAreaByPositionOrNearest(
                EntryLocation,
                Link->GetAreaResolveTolerance());
        }

        AAIAreaBase* AreaB = Link->GetAreaB();
        if (!IsValid(AreaB))
        {
            AreaB = FindAreaByPositionOrNearest(
                ExitLocation,
                Link->GetAreaResolveTolerance());
        }

        if (!IsValid(AreaA) || !AreaA->IsAreaEnabled())
        {
            BuildIssues.Add(FString::Printf(
                TEXT("%s : Entry 위치를 담당할 활성 Area A를 찾지 못했습니다. Entry=%s"),
                *Link->GetName(),
                *EntryLocation.ToCompactString()));
            continue;
        }

        if (!IsValid(AreaB) || !AreaB->IsAreaEnabled())
        {
            BuildIssues.Add(FString::Printf(
                TEXT("%s : Exit 위치를 담당할 활성 Area B를 찾지 못했습니다. Exit=%s"),
                *Link->GetName(),
                *ExitLocation.ToCompactString()));
            continue;
        }

        if (AreaA == AreaB)
        {
            BuildIssues.Add(FString::Printf(
                TEXT("%s : Entry와 Exit가 같은 Area '%s'로 판정되었습니다."),
                *Link->GetName(),
                *AreaA->GetAreaId().ToString()));
            continue;
        }

        FAreaDirectedConnection AtoB;
        AtoB.FromArea = AreaA;
        AtoB.ToArea = AreaB;
        AtoB.SourceLink = Link;
        AtoB.TraversalActor = Link->GetTraversalActorAtoB();
        AtoB.TraversalType = Link->GetTraversalTypeAtoB();
        AtoB.EntryLocation = EntryLocation;
        AtoB.ExitLocation = ExitLocation;
        AtoB.TraversalDistanceCost = Link->GetTraversalDistanceCost();
        AtoB.TraversalRiskCost = Link->GetTraversalRiskCost();
        AtoB.bEnabled = Link->IsLinkEnabled();
        AtoB.bAutomatic = false;

        if (AreaGraphServicePrivate::RequiresTraversalActor(AtoB.TraversalType)
            && !AtoB.TraversalActor.IsValid())
        {
            BuildIssues.Add(FString::Printf(
                TEXT("%s : A -> B 이동(%s)에 필요한 Traversal Actor가 없습니다."),
                *Link->GetName(),
                *UEnum::GetValueAsString(AtoB.TraversalType)));
        }
        else
        {
            AddDirectedConnection(AtoB);
        }

        if (Link->IsTwoWay())
        {
            FAreaDirectedConnection BtoA = AtoB;
            BtoA.FromArea = AreaB;
            BtoA.ToArea = AreaA;
            BtoA.TraversalActor = Link->GetTraversalActorBtoA();
            BtoA.TraversalType = Link->GetTraversalTypeBtoA();
            BtoA.EntryLocation = Link->GetEntryLocationBtoA();
            BtoA.ExitLocation = Link->GetExitLocationBtoA();

            if (AreaGraphServicePrivate::RequiresTraversalActor(BtoA.TraversalType)
                && !BtoA.TraversalActor.IsValid())
            {
                BuildIssues.Add(FString::Printf(
                    TEXT("%s : B -> A 이동(%s)에 필요한 Traversal Actor가 없습니다."),
                    *Link->GetName(),
                    *UEnum::GetValueAsString(BtoA.TraversalType)));
            }
            else
            {
                AddDirectedConnection(BtoA);
            }
        }

        // 수동 Link가 하나라도 있으면 같은 두 Area 사이에는 자동 Normal 연결을 만들지 않습니다.
        OutManualPairHashes.Add(MakeDirectedPairHash(AreaA, AreaB));
        OutManualPairHashes.Add(MakeDirectedPairHash(AreaB, AreaA));
    }
}

void FAreaGraphService::AddAutomaticNormalLinks(
    UWorld* World,
    const AAreaManagerActorBase* ManagerActor,
    const TSet<uint32>& ManualPairHashes)
{
    const float MaxGap = IsValid(ManagerActor) ? ManagerActor->GetAutoNormalMaxGap() : 300.0f;
    const float MaxHeightDifference = IsValid(ManagerActor) ? ManagerActor->GetAutoNormalMaxHeightDifference() : 120.0f;
    const float Inset = IsValid(ManagerActor) ? ManagerActor->GetAutoNormalInset() : 80.0f;
    const float BoundarySampleSpacing = IsValid(ManagerActor)
        ? ManagerActor->GetAutoNormalBoundarySampleSpacing()
        : 100.0f;
    const float MaxPathDetourRatio = IsValid(ManagerActor)
        ? ManagerActor->GetAutoNormalMaxPathDetourRatio()
        : 2.5f;
    const float MaxPathExtraDistance = IsValid(ManagerActor)
        ? ManagerActor->GetAutoNormalMaxPathExtraDistance()
        : 300.0f;
    const bool bValidateWithNavigation = !IsValid(ManagerActor)
        || ManagerActor->ShouldValidateAutoLinksWithNavigation();

    for (int32 AIndex = 0; AIndex < Areas.Num(); ++AIndex)
    {
        AAIAreaBase* AreaA = Areas[AIndex].Get();
        if (!IsValid(AreaA))
        {
            continue;
        }

        for (int32 BIndex = AIndex + 1; BIndex < Areas.Num(); ++BIndex)
        {
            AAIAreaBase* AreaB = Areas[BIndex].Get();
            if (!IsValid(AreaB))
            {
                continue;
            }

            if (ManualPairHashes.Contains(MakeDirectedPairHash(AreaA, AreaB))
                || ManualPairHashes.Contains(MakeDirectedPairHash(AreaB, AreaA)))
            {
                continue;
            }

            const FBox BoundsA = AreaA->GetAreaBounds();
            const FBox BoundsB = AreaB->GetAreaBounds();

            if (ComputeBoundsGapXY(BoundsA, BoundsB) > MaxGap)
            {
                continue;
            }

            /*
             * Area Box 중심 Z는 Box 높이와 배치 방식에 따라 실제 바닥 높이와 달라질 수 있습니다.
             * 따라서 여기서는 중심 높이로 미리 제외하지 않고, NavMesh에 투영된 실제 연결 후보
             * 두 지점의 Z 차이를 아래 TryFindBestAutomaticNormalEndpoints에서 검사합니다.
             */
            FVector PointA = AreaA->GetPointInsideTowards(
                AreaB->GetAreaCenter(),
                Inset);
            FVector PointB = AreaB->GetPointInsideTowards(
                AreaA->GetAreaCenter(),
                Inset);

            if (bValidateWithNavigation)
            {
                double PathLength = 0.0;
                FString FailureReason;

                if (!TryFindBestAutomaticNormalEndpoints(
                    World,
                    AreaA,
                    AreaB,
                    Inset,
                    BoundarySampleSpacing,
                    MaxHeightDifference,
                    MaxPathDetourRatio,
                    MaxPathExtraDistance,
                    PointA,
                    PointB,
                    PathLength,
                    FailureReason))
                {
                    BuildIssues.Add(FString::Printf(
                        TEXT(
                            "자동 Normal 제외: %s <-> %s | GapXY=%.1f, MaxNavHeightDiff=%.1f | %s"),
                        *AreaA->GetAreaId().ToString(),
                        *AreaB->GetAreaId().ToString(),
                        ComputeBoundsGapXY(BoundsA, BoundsB),
                        MaxHeightDifference,
                        *FailureReason));
                    continue;
                }
            }

            FAreaDirectedConnection AtoB;
            AtoB.FromArea = AreaA;
            AtoB.ToArea = AreaB;
            AtoB.TraversalType = EAreaTraversalType::Normal;
            AtoB.EntryLocation = PointA;
            AtoB.ExitLocation = PointB;
            AtoB.bEnabled = true;
            AtoB.bAutomatic = true;
            AddDirectedConnection(AtoB);

            FAreaDirectedConnection BtoA = AtoB;
            BtoA.FromArea = AreaB;
            BtoA.ToArea = AreaA;
            BtoA.EntryLocation = PointB;
            BtoA.ExitLocation = PointA;
            AddDirectedConnection(BtoA);
        }
    }
}

void FAreaGraphService::AddDirectedConnection(const FAreaDirectedConnection& Connection)
{
    if (AreaGraphServicePrivate::IsConnectionDataValid(Connection))
    {
        Connections.Add(Connection);
    }
}

void FAreaGraphService::RebuildAdjacency()
{
    OutgoingConnections.Reset();

    for (int32 Index = 0; Index < Connections.Num(); ++Index)
    {
        const FAreaDirectedConnection& Connection = Connections[Index];
        if (!AreaGraphServicePrivate::IsConnectionDataValid(Connection))
        {
            continue;
        }

        OutgoingConnections.FindOrAdd(Connection.FromArea.Get()).Add(Index);
    }
}

uint32 FAreaGraphService::MakeDirectedPairHash(const AAIAreaBase* FromArea, const AAIAreaBase* ToArea)
{
    return HashCombine(GetTypeHash(FromArea), GetTypeHash(ToArea));
}

void FAreaGraphService::BuildBoundaryCandidates(
    const AAIAreaBase* SourceArea,
    const AAIAreaBase* TargetArea,
    const float Inset,
    const float SampleSpacing,
    TArray<FVector>& OutCandidates)
{
    OutCandidates.Reset();

    if (!IsValid(SourceArea) || !IsValid(TargetArea))
    {
        return;
    }

    const FBox Bounds = SourceArea->GetAreaBounds();
    const FVector SourceCenter = SourceArea->GetAreaCenter();
    const FVector TargetCenter = TargetArea->GetAreaCenter();

    // 기존 중심 방향 후보를 먼저 넣어 단순한 배치에서는 빠르게 정답을 찾습니다.
    AreaGraphServicePrivate::AddCandidateUnique(
        OutCandidates,
        SourceArea->GetPointInsideTowards(TargetCenter, Inset));

    const float SafeSpacing = FMath::Max(25.0f, SampleSpacing);
    const float SizeX = FMath::Max(0.0f, Bounds.Max.X - Bounds.Min.X);
    const float SizeY = FMath::Max(0.0f, Bounds.Max.Y - Bounds.Min.Y);

    const int32 SamplesAlongX = FMath::Clamp(
        FMath::CeilToInt(SizeX / SafeSpacing) + 1,
        AreaGraphServicePrivate::AutoNormalMinSamplesPerSide,
        AreaGraphServicePrivate::AutoNormalMaxSamplesPerSide);

    const int32 SamplesAlongY = FMath::Clamp(
        FMath::CeilToInt(SizeY / SafeSpacing) + 1,
        AreaGraphServicePrivate::AutoNormalMinSamplesPerSide,
        AreaGraphServicePrivate::AutoNormalMaxSamplesPerSide);

    /*
     * 마주 보는 한쪽 면만 검사하지 않고 XY 둘레 네 면 전체를 샘플링합니다.
     * 따라서 문이 중심선에서 벗어나 있거나 옆으로 꺾인 통로도 후보에 포함됩니다.
     *
     * GetPointInsideTowards가 실제 Area Box 로컬 공간에서 Clamp하므로,
     * 여기서 사용하는 월드 AABB 샘플은 최종적으로 Area 내부 지점으로 보정됩니다.
     */
    for (int32 Index = 0; Index < SamplesAlongY; ++Index)
    {
        const float Alpha = SamplesAlongY > 1
            ? static_cast<float>(Index) / static_cast<float>(SamplesAlongY - 1)
            : 0.5f;

        const float SampleY = FMath::Lerp(Bounds.Min.Y, Bounds.Max.Y, Alpha);

        const FVector PositiveXTarget(
            Bounds.Max.X + 10000.0f,
            SampleY,
            SourceCenter.Z);

        const FVector NegativeXTarget(
            Bounds.Min.X - 10000.0f,
            SampleY,
            SourceCenter.Z);

        AreaGraphServicePrivate::AddCandidateUnique(
            OutCandidates,
            SourceArea->GetPointInsideTowards(PositiveXTarget, Inset));

        AreaGraphServicePrivate::AddCandidateUnique(
            OutCandidates,
            SourceArea->GetPointInsideTowards(NegativeXTarget, Inset));
    }

    for (int32 Index = 0; Index < SamplesAlongX; ++Index)
    {
        const float Alpha = SamplesAlongX > 1
            ? static_cast<float>(Index) / static_cast<float>(SamplesAlongX - 1)
            : 0.5f;

        const float SampleX = FMath::Lerp(Bounds.Min.X, Bounds.Max.X, Alpha);

        const FVector PositiveYTarget(
            SampleX,
            Bounds.Max.Y + 10000.0f,
            SourceCenter.Z);

        const FVector NegativeYTarget(
            SampleX,
            Bounds.Min.Y - 10000.0f,
            SourceCenter.Z);

        AreaGraphServicePrivate::AddCandidateUnique(
            OutCandidates,
            SourceArea->GetPointInsideTowards(PositiveYTarget, Inset));

        AreaGraphServicePrivate::AddCandidateUnique(
            OutCandidates,
            SourceArea->GetPointInsideTowards(NegativeYTarget, Inset));
    }
}

bool FAreaGraphService::TryProjectCandidateToNavigation(
    UWorld* World,
    const AAIAreaBase* OwningArea,
    const FVector& Candidate,
    FVector& OutProjectedPoint)
{
    OutProjectedPoint = FVector::ZeroVector;

    if (!IsValid(World) || !IsValid(OwningArea))
    {
        return false;
    }

    UNavigationSystemV1* NavigationSystem =
        FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

    if (!IsValid(NavigationSystem))
    {
        return false;
    }

    FNavLocation ProjectedLocation;
    const ANavigationData* NavData = nullptr;
    const FSharedConstNavQueryFilter QueryFilter;

    if (!NavigationSystem->ProjectPointToNavigation(
        Candidate,
        ProjectedLocation,
        AreaGraphServicePrivate::AutoNormalProjectionExtent,
        NavData,
        QueryFilter))
    {
        return false;
    }

    const FVector ProjectedPoint = ProjectedLocation.Location;

    if (!OwningArea->ContainsPosition(ProjectedPoint))
    {
        /*
         * NavMesh의 Agent Radius 때문에 경계 바로 안쪽 후보가 약간 바깥으로
         * 투영될 수 있습니다. Area와 너무 멀지 않은 경우만 허용합니다.
         */
        const FVector ClosestPoint =
            OwningArea->GetClosestPointInArea(ProjectedPoint, 0.0f);

        if (FVector::DistSquared(ProjectedPoint, ClosestPoint)
            > FMath::Square(AreaGraphServicePrivate::AutoNormalAreaTolerance))
        {
            return false;
        }
    }

    OutProjectedPoint = ProjectedPoint;
    return true;
}

bool FAreaGraphService::TryFindBestAutomaticNormalEndpoints(
    UWorld* World,
    const AAIAreaBase* AreaA,
    const AAIAreaBase* AreaB,
    const float Inset,
    const float SampleSpacing,
    const float MaxHeightDifference,
    const float MaxPathDetourRatio,
    const float MaxPathExtraDistance,
    FVector& OutPointA,
    FVector& OutPointB,
    double& OutPathLength,
    FString& OutFailureReason)
{
    OutPointA = FVector::ZeroVector;
    OutPointB = FVector::ZeroVector;
    OutPathLength = TNumericLimits<double>::Max();
    OutFailureReason.Reset();

    if (!IsValid(World) || !IsValid(AreaA) || !IsValid(AreaB))
    {
        OutFailureReason = TEXT("World 또는 Area 참조가 유효하지 않습니다.");
        return false;
    }

    TArray<FVector> RawCandidatesA;
    TArray<FVector> RawCandidatesB;
    BuildBoundaryCandidates(
        AreaA,
        AreaB,
        Inset,
        SampleSpacing,
        RawCandidatesA);
    BuildBoundaryCandidates(
        AreaB,
        AreaA,
        Inset,
        SampleSpacing,
        RawCandidatesB);

    TArray<FVector> ProjectedCandidatesA;
    TArray<FVector> ProjectedCandidatesB;

    for (const FVector& RawCandidate : RawCandidatesA)
    {
        FVector ProjectedCandidate;
        if (TryProjectCandidateToNavigation(
            World,
            AreaA,
            RawCandidate,
            ProjectedCandidate))
        {
            AreaGraphServicePrivate::AddCandidateUnique(
                ProjectedCandidatesA,
                ProjectedCandidate);
        }
    }

    for (const FVector& RawCandidate : RawCandidatesB)
    {
        FVector ProjectedCandidate;
        if (TryProjectCandidateToNavigation(
            World,
            AreaB,
            RawCandidate,
            ProjectedCandidate))
        {
            AreaGraphServicePrivate::AddCandidateUnique(
                ProjectedCandidatesB,
                ProjectedCandidate);
        }
    }

    if (ProjectedCandidatesA.IsEmpty() || ProjectedCandidatesB.IsEmpty())
    {
        OutFailureReason = FString::Printf(
            TEXT(
                "경계 후보의 NavMesh 투영 실패 "
                "[Raw A=%d, Raw B=%d, Projected A=%d, Projected B=%d]"),
            RawCandidatesA.Num(),
            RawCandidatesB.Num(),
            ProjectedCandidatesA.Num(),
            ProjectedCandidatesB.Num());
        return false;
    }

    struct FBoundaryCandidatePair
    {
        int32 AIndex = INDEX_NONE;
        int32 BIndex = INDEX_NONE;
        FVector PointA = FVector::ZeroVector;
        FVector PointB = FVector::ZeroVector;
        double DirectDistance = 0.0;
    };

    TArray<FBoundaryCandidatePair> CandidatePairs;
    TSet<uint64> AddedPairKeys;

    const auto AddPairUnique =
        [&CandidatePairs, &AddedPairKeys, &ProjectedCandidatesA, &ProjectedCandidatesB](
            const int32 AIndex,
            const int32 BIndex)
        {
            if (!ProjectedCandidatesA.IsValidIndex(AIndex)
                || !ProjectedCandidatesB.IsValidIndex(BIndex))
            {
                return;
            }

            const uint64 PairKey =
                (static_cast<uint64>(static_cast<uint32>(AIndex)) << 32)
                | static_cast<uint64>(static_cast<uint32>(BIndex));

            if (AddedPairKeys.Contains(PairKey))
            {
                return;
            }

            AddedPairKeys.Add(PairKey);

            FBoundaryCandidatePair& Pair =
                CandidatePairs.AddDefaulted_GetRef();

            Pair.AIndex = AIndex;
            Pair.BIndex = BIndex;
            Pair.PointA = ProjectedCandidatesA[AIndex];
            Pair.PointB = ProjectedCandidatesB[BIndex];
            Pair.DirectDistance =
                FVector::Distance(Pair.PointA, Pair.PointB);
        };

    const int32 NearestCount =
        AreaGraphServicePrivate::AutoNormalNearestPairsPerCandidate;

    /*
     * A의 모든 후보마다 가까운 B 후보를 선택합니다.
     * 중앙 벽 앞 후보뿐 아니라 문 근처, 옆면, 반대쪽 둘레 후보까지
     * 각각 독립적으로 NavMesh 경로 검사를 받습니다.
     */
    for (int32 AIndex = 0; AIndex < ProjectedCandidatesA.Num(); ++AIndex)
    {
        TArray<int32> SortedBIndices;
        SortedBIndices.Reserve(ProjectedCandidatesB.Num());

        for (int32 BIndex = 0; BIndex < ProjectedCandidatesB.Num(); ++BIndex)
        {
            SortedBIndices.Add(BIndex);
        }

        SortedBIndices.Sort(
            [&ProjectedCandidatesA, &ProjectedCandidatesB, AIndex](
                const int32 LeftIndex,
                const int32 RightIndex)
            {
                return FVector::DistSquared(
                        ProjectedCandidatesA[AIndex],
                        ProjectedCandidatesB[LeftIndex])
                    < FVector::DistSquared(
                        ProjectedCandidatesA[AIndex],
                        ProjectedCandidatesB[RightIndex]);
            });

        const int32 Count =
            FMath::Min(NearestCount, SortedBIndices.Num());

        for (int32 Index = 0; Index < Count; ++Index)
        {
            AddPairUnique(AIndex, SortedBIndices[Index]);
        }
    }

    /*
     * B 기준으로도 같은 과정을 수행합니다.
     * 한쪽 Area가 훨씬 크거나 후보 밀도가 다를 때 작은 Area의 출입구 후보가
     * 누락되는 것을 방지합니다.
     */
    for (int32 BIndex = 0; BIndex < ProjectedCandidatesB.Num(); ++BIndex)
    {
        TArray<int32> SortedAIndices;
        SortedAIndices.Reserve(ProjectedCandidatesA.Num());

        for (int32 AIndex = 0; AIndex < ProjectedCandidatesA.Num(); ++AIndex)
        {
            SortedAIndices.Add(AIndex);
        }

        SortedAIndices.Sort(
            [&ProjectedCandidatesA, &ProjectedCandidatesB, BIndex](
                const int32 LeftIndex,
                const int32 RightIndex)
            {
                return FVector::DistSquared(
                        ProjectedCandidatesB[BIndex],
                        ProjectedCandidatesA[LeftIndex])
                    < FVector::DistSquared(
                        ProjectedCandidatesB[BIndex],
                        ProjectedCandidatesA[RightIndex]);
            });

        const int32 Count =
            FMath::Min(NearestCount, SortedAIndices.Num());

        for (int32 Index = 0; Index < Count; ++Index)
        {
            AddPairUnique(SortedAIndices[Index], BIndex);
        }
    }

    CandidatePairs.Sort(
        [](const FBoundaryCandidatePair& Left, const FBoundaryCandidatePair& Right)
        {
            return Left.DirectDistance < Right.DirectDistance;
        });

    const double SafeMaxHeightDifference =
        FMath::Max(0.0, static_cast<double>(MaxHeightDifference));
    const double SafeDetourRatio =
        FMath::Max(1.0, static_cast<double>(MaxPathDetourRatio));
    const double SafeExtraDistance =
        FMath::Max(0.0, static_cast<double>(MaxPathExtraDistance));

    bool bFoundPath = false;
    int32 HeightRejectedCount = 0;
    int32 PathQuerySuccessCount = 0;
    int32 DetourRejectedCount = 0;

    for (const FBoundaryCandidatePair& Pair : CandidatePairs)
    {
        /*
         * 두 Area Box 중심이 아니라 NavMesh에 투영된 실제 연결 후보의 높이를 비교합니다.
         * 같은 바닥에 있는 높이가 다른 Box는 정상 연결되고, 실제 단차가 큰 후보만 제외됩니다.
         */
        const double CandidateHeightDifference =
            FMath::Abs(static_cast<double>(Pair.PointA.Z - Pair.PointB.Z));

        if (CandidateHeightDifference > SafeMaxHeightDifference)
        {
            ++HeightRejectedCount;
            continue;
        }

        double CandidatePathLength = 0.0;
        if (!TryGetNavigationPathLength(
            World,
            Pair.PointA,
            Pair.PointB,
            CandidatePathLength))
        {
            continue;
        }

        ++PathQuerySuccessCount;

        /*
         * 완전한 벽을 맵 반대편으로 크게 우회해 도달하는 경우를
         * 바로 인접한 Area 연결로 오인하지 않도록 허용 우회 길이를 제한합니다.
         */
        const double MaxAllowedPathLength =
            Pair.DirectDistance * SafeDetourRatio + SafeExtraDistance;

        if (CandidatePathLength > MaxAllowedPathLength)
        {
            ++DetourRejectedCount;
            continue;
        }

        if (!bFoundPath || CandidatePathLength < OutPathLength)
        {
            bFoundPath = true;
            OutPathLength = CandidatePathLength;
            OutPointA = Pair.PointA;
            OutPointB = Pair.PointB;
        }
    }

    if (!bFoundPath)
    {
        OutFailureReason = FString::Printf(
            TEXT(
                "분산 후보 경로를 찾지 못했습니다 "
                "[Raw A=%d, Raw B=%d, Projected A=%d, Projected B=%d, "
                "PairTests=%d, HeightRejected=%d, NavPathSuccess=%d, DetourRejected=%d]"),
            RawCandidatesA.Num(),
            RawCandidatesB.Num(),
            ProjectedCandidatesA.Num(),
            ProjectedCandidatesB.Num(),
            CandidatePairs.Num(),
            HeightRejectedCount,
            PathQuerySuccessCount,
            DetourRejectedCount);
    }

    return bFoundPath;
}

float FAreaGraphService::ComputeBoundsGapXY(const FBox& A, const FBox& B)
{
    const float GapX = FMath::Max(0.0f, FMath::Max(A.Min.X - B.Max.X, B.Min.X - A.Max.X));
    const float GapY = FMath::Max(0.0f, FMath::Max(A.Min.Y - B.Max.Y, B.Min.Y - A.Max.Y));
    return FVector2D(GapX, GapY).Size();
}

bool FAreaGraphService::TryGetNavigationPathLength(
    UWorld* World,
    const FVector& Start,
    const FVector& End,
    double& OutLength)
{
    OutLength = 0.0;

    if (!IsValid(World))
    {
        return false;
    }

    const ENavigationQueryResult::Type Result = UNavigationSystemV1::GetPathLength(
        World,
        Start,
        End,
        OutLength,
        nullptr,
        TSubclassOf<UNavigationQueryFilter>());

    return Result == ENavigationQueryResult::Success;
}
