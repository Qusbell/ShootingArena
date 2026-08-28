#include "GameplayValidationSubsystem.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameplayValidator, Log, All);

void UGameplayValidationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FEditorDelegates::OnMapOpened.AddUObject(this, &UGameplayValidationSubsystem::HandleMapOpened);
}

void UGameplayValidationSubsystem::Deinitialize()
{
    FEditorDelegates::OnMapOpened.RemoveAll(this);
    ClearReport();
    Super::Deinitialize();
}

UWorld* UGameplayValidationSubsystem::GetEditorWorld() const
{
    if (!GEditor)
    {
        return nullptr;
    }

    return GEditor->GetEditorWorldContext().World();
}

const FGameplayValidationReport& UGameplayValidationSubsystem::ValidateCurrentLevel()
{
    LastReport.Reset();
    ConfigurationMessages.Reset();

    UWorld* World = GetEditorWorld();
    if (!IsValid(World))
    {
        UE_LOG(LogGameplayValidator, Warning, TEXT("[GameplayValidator] Editor World를 찾을 수 없습니다."));
        ReportChanged.Broadcast();
        return LastReport;
    }

    const double StartSeconds = FPlatformTime::Seconds();

    FGameplayValidationContext Context;
    if (!FGameplayValidationWorldScanner::BuildContext(
        World,
        ProviderRegistry,
        Context,
        ConfigurationMessages))
    {
        ReportChanged.Broadcast();
        return LastReport;
    }

    LastReport.bHasRun = true;
    LastReport.LevelName = World->GetMapName();
    LastReport.TotalCheckedActors = Context.GetTotalActorCount();

    // 등록 슬롯별 배치 개수를 Report에 저장합니다.
    for (const TSharedRef<IGameplayValidationProvider>& Provider : ProviderRegistry.GetProviders())
    {
        TArray<FGameplayValidationTargetSlot> Slots;
        Provider->GetTargetSlots(Slots);

        for (const FGameplayValidationTargetSlot& Slot : Slots)
        {
            FGameplayValidationTargetCount& Count = LastReport.TargetCounts.AddDefaulted_GetRef();
            Count.Key = FGameplayValidationTargetKey(Provider->GetProviderId(), Slot.SlotId);
            Count.ProviderDisplayName = Provider->GetDisplayName();
            Count.SlotDisplayName = Slot.DisplayName;
            Count.Count = Context.GetActorCount(Count.Key.ProviderId, Count.Key.SlotId);
        }
    }

    // 어떤 Provider도 Tick하지 않습니다. 버튼 클릭 시 Prepare -> Validate -> Finalize 한 번만 실행합니다.
    for (const TSharedRef<IGameplayValidationProvider>& Provider : ProviderRegistry.GetProviders())
    {
        Provider->Prepare(Context);
    }

    for (const TSharedRef<IGameplayValidationProvider>& Provider : ProviderRegistry.GetProviders())
    {
        Provider->Validate(Context, LastReport.Issues);
    }

    for (const TSharedRef<IGameplayValidationProvider>& Provider : ProviderRegistry.GetProviders())
    {
        Provider->Finalize(Context, LastReport.Issues);
        Provider->GetSummaryMetrics(LastReport.Metrics);
    }

    LastReport.Issues.Sort([](const FGameplayValidationIssue& A, const FGameplayValidationIssue& B)
    {
        if (A.Severity != B.Severity)
        {
            return A.Severity == EGameplayValidationSeverity::Error;
        }
        if (A.ProviderId != B.ProviderId)
        {
            return A.ProviderId.ToString() < B.ProviderId.ToString();
        }
        return A.RuleId.ToString() < B.RuleId.ToString();
    });

    LastReport.Metrics.Sort([](const FGameplayValidationMetric& A, const FGameplayValidationMetric& B)
    {
        if (A.ProviderId != B.ProviderId)
        {
            return A.ProviderId.ToString() < B.ProviderId.ToString();
        }
        return A.SortOrder < B.SortOrder;
    });

    LastReport.ElapsedMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

    UE_LOG(
        LogGameplayValidator,
        Verbose,
        TEXT("[GameplayValidator] Level=%s | Actors=%d | Errors=%d | Warnings=%d | Time=%.3fms"),
        *LastReport.LevelName,
        LastReport.TotalCheckedActors,
        LastReport.GetIssueCount(EGameplayValidationSeverity::Error),
        LastReport.GetIssueCount(EGameplayValidationSeverity::Warning),
        LastReport.ElapsedMilliseconds);

    ReportChanged.Broadcast();
    return LastReport;
}

void UGameplayValidationSubsystem::ClearReport()
{
    LastReport.Reset();
    ConfigurationMessages.Reset();
    ReportChanged.Broadcast();
}

void UGameplayValidationSubsystem::HandleMapOpened(const FString& Filename, const bool bAsTemplate)
{
    // 이전 레벨 결과가 새 레벨에 남지 않도록 지우기만 하고 자동 검사는 수행하지 않습니다.
    ClearReport();
}
