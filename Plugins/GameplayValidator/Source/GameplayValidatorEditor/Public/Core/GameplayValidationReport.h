#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationIssue.h"
#include "Core/GameplayValidationTypes.h"

struct FGameplayValidationTargetCount
{
    FGameplayValidationTargetKey Key;
    FText ProviderDisplayName;
    FText SlotDisplayName;
    int32 Count = 0;
};

/** 한 번의 Validate Current Level 실행 결과 전체입니다. */
struct FGameplayValidationReport
{
    FString LevelName;
    bool bHasRun = false;
    double ElapsedMilliseconds = 0.0;
    int32 TotalCheckedActors = 0;

    TArray<FGameplayValidationIssue> Issues;
    TArray<FGameplayValidationTargetCount> TargetCounts;
    TArray<FGameplayValidationMetric> Metrics;

    void Reset()
    {
        LevelName.Reset();
        bHasRun = false;
        ElapsedMilliseconds = 0.0;
        TotalCheckedActors = 0;
        Issues.Reset();
        TargetCounts.Reset();
        Metrics.Reset();
    }

    int32 GetIssueCount(const EGameplayValidationSeverity Severity) const
    {
        // UE 5.6의 TArray에는 CountByPredicate 멤버 함수가 없으므로
        // 직접 순회해서 해당 Severity의 Issue 개수를 계산합니다.
        int32 Count = 0;

        for (const FGameplayValidationIssue& Issue : Issues)
        {
            if (Issue.Severity == Severity)
            {
                ++Count;
            }
        }

        return Count;
    }

    bool IsSuccess() const
    {
        return bHasRun
            && GetIssueCount(EGameplayValidationSeverity::Error) == 0
            && GetIssueCount(EGameplayValidationSeverity::Warning) == 0;
    }
};
