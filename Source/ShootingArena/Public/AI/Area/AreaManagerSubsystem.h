#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "AI/Area/AreaTypes.h"
#include "AreaManagerSubsystem.generated.h"

class AAIAreaBase;
class AAreaManagerActorBase;
class AAIController;
class AActor;
class UAreaRiskConfigDataAsset;
class FAreaGraphService;
class FAreaRiskService;
class FAreaRouteFinder;

/**
 * 콘솔 위험도 디버그에서 이전 값과 현재 값을 비교하기 위한 내부 스냅샷입니다.
 * UObject 수명과 무관한 정수/Bool만 저장하므로 UPROPERTY가 필요하지 않습니다.
 */
struct FAreaRiskDebugSnapshot
{
    int32 AreaRiskScore = 0;
    int32 StructuralRiskScore = 0;
    int32 DynamicRiskScore = 0;
    int32 ConnectedAreaCount = 0;
    int32 RecognizedUnitCount = 0;
    int32 UnitCountScore = 0;
    int32 CombatScore = 0;

    bool bIsDeadEnd = false;
    bool bIsAreaRecognized = false;
    bool bIsCombatDetected = false;

    static FAreaRiskDebugSnapshot FromResult(
        const FAreaRiskScoreResult& Result)
    {
        FAreaRiskDebugSnapshot Snapshot;
        Snapshot.AreaRiskScore = Result.AreaRiskScore;
        Snapshot.StructuralRiskScore = Result.StructuralRiskScore;
        Snapshot.DynamicRiskScore = Result.DynamicRiskScore;
        Snapshot.ConnectedAreaCount = Result.ConnectedAreaCount;
        Snapshot.RecognizedUnitCount = Result.RecognizedUnitCount;
        Snapshot.UnitCountScore = Result.UnitCountScore;
        Snapshot.CombatScore = Result.CombatScore;
        Snapshot.bIsDeadEnd = Result.bIsDeadEnd;
        Snapshot.bIsAreaRecognized = Result.bIsAreaRecognized;
        Snapshot.bIsCombatDetected = Result.bIsCombatDetected;
        return Snapshot;
    }

    bool Equals(const FAreaRiskDebugSnapshot& Other) const
    {
        return AreaRiskScore == Other.AreaRiskScore
            && StructuralRiskScore == Other.StructuralRiskScore
            && DynamicRiskScore == Other.DynamicRiskScore
            && ConnectedAreaCount == Other.ConnectedAreaCount
            && RecognizedUnitCount == Other.RecognizedUnitCount
            && UnitCountScore == Other.UnitCountScore
            && CombatScore == Other.CombatScore
            && bIsDeadEnd == Other.bIsDeadEnd
            && bIsAreaRecognized == Other.bIsAreaRecognized
            && bIsCombatDetected == Other.bIsCombatDetected;
    }
};

/** BT Service가 계산한 후퇴 경로를 Task가 재사용하기 위한 짧은 수명의 내부 캐시입니다. */
struct FCachedAreaRetreatRoute
{
    TWeakObjectPtr<AAIAreaBase> StartArea;
    FVector StartPosition = FVector::ZeroVector;
    double StoredWorldTime = 0.0;
    FAreaRouteResult RouteResult;
};

/**
 * 외부 Blueprint와 AI 로직이 Area 기능을 호출하는 단일 진입점입니다.
 * 실제 계산은 내부 C++ Service가 담당합니다.
 */
UCLASS()
class SHOOTINGARENA_API UAreaManagerSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    UAreaManagerSubsystem();
    virtual ~UAreaManagerSubsystem() override;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    /** 현재 월드의 Manager/Area/Link 정보와 DA_Area_Risk 참조를 다시 읽습니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area")
    bool RebuildRuntimeGraph();

    UFUNCTION(BlueprintPure, Category = "AI|Area")
    bool IsGraphReady() const { return bGraphReady; }

    /** 위치를 포함하는 Area를 반환합니다. 겹치는 Area가 있으면 더 작은 Area를 우선합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area")
    AAIAreaBase* GetAreaByPosition(const FVector& WorldPosition) const;

    /** PPT 공식으로 계산한 최종 위험도 점수만 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Risk")
    float GetAreaRiskScore(
        AAIAreaBase* Area,
        AAIController* ObserverController) const;

    /** PPT 표의 구조/동적 위험도 세부 계산 결과를 반환합니다. */
    UFUNCTION(BlueprintPure, Category = "AI|Area|Risk")
    FAreaRiskScoreResult GetAreaRiskScoreDetails(
        AAIAreaBase* Area,
        AAIController* ObserverController) const;

    /**
     * BB_AIMemory.Recognized_Areas가 변경될 때 호출합니다.
     * false로 변경하면 해당 Area의 인식 유닛과 교전 정보도 함께 제거합니다.
     */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Risk")
    bool SetAreaRecognized(
        AAIAreaBase* Area,
        AAIController* ObserverController,
        bool bRecognized);

    /** 위치를 사용해 Recognized_Areas를 변경하는 편의 함수입니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Risk")
    bool SetAreaRecognizedByPosition(
        const FVector& AreaPosition,
        AAIController* ObserverController,
        bool bRecognized);

    /**
     * AI 시야 처리로 생존 유닛을 확인했을 때 호출합니다.
     * 같은 유닛을 다시 호출하면 중복 추가하지 않고 현재 Area만 갱신합니다.
     */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Risk")
    bool RecognizeUnit(
        const FVector& UnitLocation,
        AActor* UnitActor,
        AAIController* ObserverController);

    /** 시야에서 놓쳤거나 사망이 확인된 유닛을 인식 목록에서 제거합니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Risk")
    bool ForgetRecognizedUnit(
        AActor* UnitActor,
        AAIController* ObserverController);

    /**
     * AI가 인식한 Area에서 사격했거나 피격당한 유닛을 확인했을 때 호출합니다.
     * 이 함수는 동일 Area의 교전 Bool을 true로 만들며 점수를 누적하지 않습니다.
     */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Risk")
    bool MarkCombatDetected(
        const FVector& EventLocation,
        AAIController* ObserverController);

    /** AI 기억 로직에서 교전 정보가 더 이상 유효하지 않다고 판단했을 때 호출합니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Risk")
    bool ClearCombatDetected(
        AAIAreaBase* Area,
        AAIController* ObserverController);

    /** 위치를 사용해 교전 상태를 해제하는 편의 함수입니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Risk")
    bool ClearCombatDetectedByPosition(
        const FVector& AreaPosition,
        AAIController* ObserverController);

    /** AI 사망, UnPossess, 풀 반환 시 해당 AI의 Area 인식 정보를 모두 제거합니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Risk")
    void RemoveObserverRiskMemory(AAIController* ObserverController);

    /** 파괴된 참조와 Lifetime이 지난 유닛/교전 기억을 실제 배열에서 정리합니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Risk")
    void CleanupInvalidRiskMemory();

    /** 현재 위치부터 목표 위치까지 위험도 우선, 거리 차선으로 전체 Area 경로를 계산합니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Route")
    bool FindSafestAreaRoute(
        const FAreaRouteRequest& Request,
        FAreaRouteResult& OutResult) const;

    /** Actor 목표를 사용하는 편의 함수입니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Route")
    bool FindSafestAreaRouteToActor(
        const FVector& StartPosition,
        AActor* TargetActor,
        AAIController* ObserverController,
        const FAreaTraversalCapabilities& TraversalCapabilities,
        FAreaRouteResult& OutResult) const;

    /**
     * Area와 Link의 위험도 계산을 완전히 생략하고, 저장된 이동거리만으로 최단 경로를 계산합니다.
     * 전투/추격처럼 위험도보다 도착 속도가 중요한 이동에 사용합니다.
     * Request.ObserverController와 Request.bIncludeStartAreaRisk는 이 함수에서 사용하지 않습니다.
     */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Route")
    bool FindAreaRoute(
        const FAreaRouteRequest& Request,
        FAreaRouteResult& OutResult) const;

    /** 위험도 없는 최단 경로를 Actor 목표로 계산하는 편의 함수입니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Route")
    bool FindAreaRouteToActor(
        const FVector& StartPosition,
        AActor* TargetActor,
        const FAreaTraversalCapabilities& TraversalCapabilities,
        FAreaRouteResult& OutResult) const;

    /** 현재 등록된 모든 Area를 반환합니다. */
    UFUNCTION(BlueprintCallable, Category = "AI|Area|Debug")
    TArray<AAIAreaBase*> GetRegisteredAreas() const;

    // 아래 함수들은 BT 내부 최적화용 C++ API이며 Blueprint 연결을 새로 만들 필요가 없습니다.
    bool GetBestBakedRetreatPoint(
        AAIAreaBase* Area,
        const FVector& FromPosition,
        FVector& OutPoint) const;

    void StoreRetreatRouteCache(
        AAIController* ObserverController,
        const FVector& StartPosition,
        const FAreaRouteResult& RouteResult);

    bool TryGetRetreatRouteCache(
        AAIController* ObserverController,
        const FVector& CurrentPosition,
        FAreaRouteResult& OutRouteResult) const;

    void ClearRetreatRouteCache(AAIController* ObserverController);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    AAreaManagerActorBase* FindManagerActor() const;
    bool CanWriteAuthoritativeRisk() const;

    /** 1초 주기로 콘솔 위험도 디버그 설정을 확인합니다. */
    void HandleAreaRiskDebugTimer();

    /**
     * 모든 AIController와 모든 등록 Area의 실제 위험도 계산 결과를 출력합니다.
     * bOnlyChanged가 true이면 이전 출력 이후 달라진 값만 출력합니다.
     */
    void DumpAreaRiskDebug(bool bOnlyChanged);

    /** 콘솔 Filter 문자열에 현재 AIController가 해당하는지 검사합니다. */
    bool PassesAreaRiskDebugFilter(
        const AAIController* ObserverController) const;

    /** 로그와 스냅샷 Map에 사용할 안정적인 Controller 식별 문자열입니다. */
    static FString GetAreaRiskDebugObserverName(
        const AAIController* ObserverController);

    /** 로그에 사용할 Area 식별 문자열입니다. */
    static FString GetAreaRiskDebugAreaName(
        const AAIAreaBase* Area);

    /** 디버그를 다시 켰을 때 전체 초기 상태가 출력되도록 이전 기록을 비웁니다. */
    void ResetAreaRiskDebugState();

    /** 내부 Service 객체를 안전한 순서로 삭제합니다. */
    void DestroyServices();

    /**
     * UObject가 아닌 내부 Service입니다.
     * UCLASS generated.cpp에서 전방 선언 타입의 TUniquePtr가 C4150을 일으킬 수 있어,
     * 포인터만 보관하고 생성/삭제는 완전한 타입을 아는 cpp에서 담당합니다.
     */
    FAreaGraphService* GraphService = nullptr;
    FAreaRiskService* RiskService = nullptr;
    FAreaRouteFinder* RouteFinder = nullptr;

    /** 현재 Manager Actor에서 읽어 온 DA_Area_Risk 참조입니다. */
    UPROPERTY(Transient)
    TObjectPtr<UAreaRiskConfigDataAsset> CachedRiskConfig = nullptr;

    /** 콘솔 디버그를 가볍게 폴링하는 월드 타이머입니다. */
    FTimerHandle AreaRiskDebugTimerHandle;

    /** ControllerPath|AreaPath별 마지막 위험도 결과입니다. */
    TMap<FString, FAreaRiskDebugSnapshot> LastAreaRiskDebugSnapshots;

    /** ControllerPath별 마지막 Current Area입니다. */
    TMap<FString, FName> LastAreaRiskDebugCurrentAreas;

    /** Debug CVar가 직전 타이머에도 켜져 있었는지 나타냅니다. */
    bool bWasAreaRiskDebugEnabled = false;

    TMap<TWeakObjectPtr<AAIController>, FCachedAreaRetreatRoute> CachedRetreatRoutes;
    bool bGraphReady = false;
};
