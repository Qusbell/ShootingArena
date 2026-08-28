#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationIssue.h"
#include "Widgets/SCompoundWidget.h"

class UGameplayValidationSubsystem;
class SBox;
class SSearchBox;
class ITableRow;
class STableViewBase;

enum class EGameplayValidatorSeverityFilter : uint8
{
    All,
    Error,
    Warning
};

struct FGameplayValidatorCategoryOption
{
    FText Label;
    FName ProviderId = NAME_None;
    FName SlotId = NAME_None;
    bool bAll = false;
    bool bProviderOnly = false;
};

class SGameplayValidatorPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGameplayValidatorPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SGameplayValidatorPanel() override;

private:
    UGameplayValidationSubsystem* GetSubsystem() const;

    TSharedRef<SWidget> BuildTargetSettings();
    TSharedRef<SWidget> BuildTargetSlotRow(FName ProviderId, FName SlotId, const FText& Label);
    TSharedRef<SWidget> BuildFilterBar();
    TSharedRef<SWidget> BuildResultsWidget();
    TSharedRef<SWidget> BuildIssueSection(EGameplayValidationSeverity Severity, const TArray<TSharedPtr<FGameplayValidationIssue>>& Items);
    TSharedRef<SWidget> BuildSummaryWidget() const;

    FReply HandleValidateClicked();
    FReply HandleResetFiltersClicked();
    void HandleReportChanged();
    void HandleSearchChanged(const FText& NewText);
    void HandleTargetClassSet(FName ProviderId, FName SlotId, const UClass* NewClass);

    UClass* GetTargetClass(FName ProviderId, FName SlotId) const;
    void RebuildFilterOptions();
    void RefreshFilteredIssues();
    bool PassesFilters(const FGameplayValidationIssue& Issue) const;

    TSharedRef<ITableRow> GenerateIssueRow(
        TSharedPtr<FGameplayValidationIssue> Item,
        const TSharedRef<STableViewBase>& OwnerTable);

    TSharedPtr<SBox> ResultHost;
    TSharedPtr<SSearchBox> SearchBox;

    TArray<TSharedPtr<FGameplayValidationIssue>> ErrorItems;
    TArray<TSharedPtr<FGameplayValidationIssue>> WarningItems;

    TArray<TSharedPtr<FGameplayValidatorCategoryOption>> CategoryOptions;
    TSharedPtr<FGameplayValidatorCategoryOption> SelectedCategory;

    TArray<TSharedPtr<EGameplayValidatorSeverityFilter>> SeverityOptions;
    TSharedPtr<EGameplayValidatorSeverityFilter> SelectedSeverity;

    FString SearchText;
    FDelegateHandle ReportChangedHandle;
};
