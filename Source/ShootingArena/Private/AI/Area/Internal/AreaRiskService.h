#pragma once

#include "CoreMinimal.h"
#include "AI/Area/AreaTypes.h"

class AAIAreaBase;
class AAIController;
class AActor;
class UAreaRiskConfigDataAsset;
class FAreaGraphService;

/** AI가 기억 중인 생존 유닛과 마지막 확인 시간을 저장합니다. */
struct FAreaRecognizedUnitRuntime
{
    TWeakObjectPtr<AActor> UnitActor;
    TWeakObjectPtr<AAIAreaBase> Area;

    /** RecognizeUnit이 마지막으로 호출된 월드 시간입니다. */
    double LastObservedTime = 0.0;
};

/** AI가 특정 Area에서 교전을 마지막으로 확인한 시간을 저장합니다. */
struct FAreaCombatMemoryRuntime
{
    TWeakObjectPtr<AAIAreaBase> Area;

    /** MarkCombatDetected가 마지막으로 호출된 월드 시간입니다. */
    double LastObservedTime = 0.0;
};

/**
 * PPT의 BB_AIMemory.Recognized_Areas에 대응하는 AI별 Area 인식 정보입니다.
 *
 * 유닛 기억과 교전 기억은 Data Asset에 설정된 Lifetime이 지나면
 * 위험도 계산에서 즉시 제외됩니다. 별도의 매 프레임 Tick은 사용하지 않습니다.
 */
struct FAreaObserverRiskMemory
{
    TWeakObjectPtr<AAIController> ObserverController;
    TArray<TWeakObjectPtr<AAIAreaBase>> RecognizedAreas;
    TArray<FAreaRecognizedUnitRuntime> RecognizedUnits;
    TArray<FAreaCombatMemoryRuntime> CombatMemories;
};

/**
 * PPT 기준 위험도 계산을 담당합니다.
 *
 * 구조 위험도:
 *   연결된 구역 수가 1개 이하이면 Dead_End_Score
 *
 * 동적 위험도:
 *   유효한 인식 유닛 수 * Unit_Count_Weight
 *   + 유효한 교전 기억이 있으면 Combat_Occurrence_Score
 *
 * 유닛/교전 기억은 마지막 확인 시간이 갱신되지 않으면 자동 만료됩니다.
 */
class FAreaRiskService
{
public:
    explicit FAreaRiskService(FAreaGraphService& InGraphService);

    /** DA_Area_Risk로 사용할 설정 에셋을 지정합니다. */
    void SetRiskConfig(UAreaRiskConfigDataAsset* InRiskConfig);

    /** 해당 AI의 Recognized_Areas 목록에 Area를 추가하거나 제거합니다. */
    bool SetAreaRecognized(
        AAIAreaBase* Area,
        AAIController* ObserverController,
        bool bRecognized);

    /**
     * 시야 처리로 확인한 생존 유닛을 등록합니다.
     * 같은 유닛을 다시 확인하면 Area와 마지막 확인 시간이 갱신됩니다.
     */
    bool RecognizeUnit(
        const FVector& UnitLocation,
        AActor* UnitActor,
        AAIController* ObserverController);

    /** 시야 이탈 또는 사망 확인 시 해당 유닛 기억을 즉시 제거합니다. */
    bool ForgetRecognizedUnit(
        AActor* UnitActor,
        AAIController* ObserverController);

    /**
     * AI가 인식한 Area에서 사격 또는 피격을 확인했을 때 호출합니다.
     * 같은 Area에서 다시 호출하면 교전 기억 시간이 갱신됩니다.
     */
    bool MarkCombatDetected(
        const FVector& EventLocation,
        AAIController* ObserverController);

    /** 필요할 때 해당 Area의 교전 기억을 즉시 제거합니다. */
    bool ClearCombatDetected(
        AAIAreaBase* Area,
        AAIController* ObserverController);

    /** PPT 표의 세부 항목을 모두 계산해 반환합니다. 만료된 기억은 계산에서 제외합니다. */
    FAreaRiskScoreResult GetAreaRiskScoreDetails(
        const AAIAreaBase* Area,
        const AAIController* ObserverController) const;

    /** 경로 탐색에서 사용하는 최종 위험도 점수입니다. */
    float GetAreaRiskScore(
        const AAIAreaBase* Area,
        const AAIController* ObserverController) const;

    /** AI가 제거되거나 새 생명으로 초기화될 때 해당 AI의 모든 인식 정보를 제거합니다. */
    void RemoveObserverMemory(const AAIController* ObserverController);

    /** 파괴된 참조와 Lifetime이 지난 유닛/교전 기억을 실제 배열에서 정리합니다. */
    void CleanupInvalidMemory();

    void Reset();

private:
    FAreaObserverRiskMemory* FindObserverMemory(const AAIController* ObserverController);
    const FAreaObserverRiskMemory* FindObserverMemory(const AAIController* ObserverController) const;
    FAreaObserverRiskMemory& FindOrAddObserverMemory(AAIController* ObserverController);

    int32 CountConnectedAreas(const AAIAreaBase* Area) const;

    /** Observer 또는 Area의 World에서 현재 게임 시간을 가져옵니다. */
    static double GetCurrentWorldTime(
        const AAIController* ObserverController,
        const AAIAreaBase* Area);

    bool IsUnitMemoryActive(
        const FAreaRecognizedUnitRuntime& UnitMemory,
        double CurrentTime) const;

    bool IsCombatMemoryActive(
        const FAreaCombatMemoryRuntime& CombatMemory,
        double CurrentTime) const;

    static bool ContainsArea(
        const TArray<TWeakObjectPtr<AAIAreaBase>>& Areas,
        const AAIAreaBase* Area);

    static void AddAreaUnique(
        TArray<TWeakObjectPtr<AAIAreaBase>>& Areas,
        AAIAreaBase* Area);

    static void RemoveArea(
        TArray<TWeakObjectPtr<AAIAreaBase>>& Areas,
        const AAIAreaBase* Area);

    FAreaGraphService& GraphService;
    TWeakObjectPtr<UAreaRiskConfigDataAsset> RiskConfig;
    TArray<FAreaObserverRiskMemory> ObserverMemories;
};
