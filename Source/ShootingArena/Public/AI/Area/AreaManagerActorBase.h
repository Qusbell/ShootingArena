#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/Area/AreaTypes.h"
#include "AreaManagerActorBase.generated.h"

class UAreaRiskConfigDataAsset;

/**
 * 맵에 하나 배치하는 Area 시스템 설정/에디터 관리 Actor입니다.
 * Rebuild 버튼으로 자동 Normal 연결과 수동 특수 Link를 맵 데이터에 저장합니다.
 */
UCLASS(Blueprintable)
class SHOOTINGARENA_API AAreaManagerActorBase : public AActor
{
    GENERATED_BODY()

public:
    AAreaManagerActorBase();

    /** 현재 맵의 Area/Link를 읽어 BakedConnections를 다시 생성합니다. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "AI|Area|Editor")
    void RebuildAreaGraph();

    /** AreaId 중복, 잘못된 Link, DA_Area_Risk 누락 등을 검사합니다. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "AI|Area|Editor")
    void ValidateAreaGraph();

    /** 저장된 연결 데이터를 비웁니다. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "AI|Area|Editor")
    void ClearBakedAreaGraph();

    /** 현재 연결을 월드에 선으로 표시합니다. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "AI|Area|Editor")
    void DrawAreaGraphDebug();

    const TArray<FAreaBakedConnection>& GetBakedConnections() const { return BakedConnections; }

    bool ShouldPreferBakedGraph() const { return bPreferBakedGraph; }
    bool ShouldBuildAutomaticNormalLinks() const { return bBuildAutomaticNormalLinks; }
    bool ShouldValidateAutoLinksWithNavigation() const { return bValidateAutoLinksWithNavigation; }
    float GetAutoNormalMaxGap() const { return AutoNormalMaxGap; }
    float GetAutoNormalMaxHeightDifference() const { return AutoNormalMaxHeightDifference; }
    float GetAutoNormalInset() const { return AutoNormalInset; }
    float GetAutoNormalBoundarySampleSpacing() const { return AutoNormalBoundarySampleSpacing; }
    float GetAutoNormalMaxPathDetourRatio() const { return AutoNormalMaxPathDetourRatio; }
    float GetAutoNormalMaxPathExtraDistance() const { return AutoNormalMaxPathExtraDistance; }
    float GetDebugDrawDuration() const { return DebugDrawDuration; }
    UAreaRiskConfigDataAsset* GetRiskConfig() const { return RiskConfig; }

protected:
    /** true이면 런타임에서 저장된 BakedConnections를 우선 사용합니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Graph")
    bool bPreferBakedGraph = true;

    /** 인접한 Area 사이의 Normal 연결을 자동 생성할지 결정합니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Graph")
    bool bBuildAutomaticNormalLinks = true;

    /** 자동 Normal 후보를 실제 NavMesh 경로가 존재할 때만 연결합니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Graph")
    bool bValidateAutoLinksWithNavigation = true;

    /** 두 Area Bounds 사이의 XY 간격이 이 값 이하일 때 자동 연결 후보로 봅니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Graph", meta = (ClampMin = "0.0"))
    float AutoNormalMaxGap = 300.0f;

    /**
     * 자동 Normal 연결에서 허용할 실제 NavMesh 연결 지점 사이의 높이 차이입니다.
     * Area Box 중심 높이는 사용하지 않으며, 이 값보다 큰 이동은 Jump/Drop 수동 Link로 둡니다.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Graph", meta = (ClampMin = "0.0"))
    float AutoNormalMaxHeightDifference = 120.0f;

    /** 자동 연결 지점을 Area 경계보다 안쪽으로 밀어 넣는 거리입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Graph", meta = (ClampMin = "0.0"))
    float AutoNormalInset = 80.0f;

    /**
     * 자동 Normal 연결 후보를 Area 전체 둘레에 배치할 간격입니다.
     * 값이 작을수록 좁은 문과 통로를 더 잘 찾지만 Rebuild 비용이 증가합니다.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Graph",
        meta = (ClampMin = "25.0", UIMin = "50.0", UIMax = "500.0"))
    float AutoNormalBoundarySampleSpacing = 100.0f;

    /**
     * 멀리 우회해야만 도달하는 Area가 바로 이웃으로 등록되는 것을 막는 비율입니다.
     * 허용 경로 길이 = 두 후보의 직선거리 * 이 값 + Extra Distance입니다.
     */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Graph",
        meta = (ClampMin = "1.0", UIMin = "1.0", UIMax = "5.0"))
    float AutoNormalMaxPathDetourRatio = 2.5f;

    /** 짧은 문턱과 복도 굴곡을 허용하기 위해 우회 길이에 추가하는 고정 거리입니다. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Graph",
        meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1000.0"))
    float AutoNormalMaxPathExtraDistance = 300.0f;

    /**
     * PPT의 DA_Area_Risk에 해당하는 공통 점수 설정입니다.
     * Dead_End_Score, Unit_Count_Weight, Combat_Occurrence_Score 세 값만 보관합니다.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Area|Risk", meta = (DisplayName = "Area Risk Data"))
    TObjectPtr<UAreaRiskConfigDataAsset> RiskConfig = nullptr;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Debug", meta = (ClampMin = "0.0"))
    float DebugDrawDuration = 10.0f;

    /** RebuildAreaGraph를 눌렀을 때 맵에 저장되는 방향성 연결 목록입니다. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "AI|Area|Graph")
    TArray<FAreaBakedConnection> BakedConnections;
};
