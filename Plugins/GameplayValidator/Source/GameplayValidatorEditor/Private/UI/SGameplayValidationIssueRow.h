#pragma once

#include "CoreMinimal.h"
#include "Core/GameplayValidationIssue.h"
#include "Widgets/Views/STableRow.h"

class STableViewBase;

class SGameplayValidationIssueRow : public SMultiColumnTableRow<TSharedPtr<FGameplayValidationIssue>>
{
public:
    SLATE_BEGIN_ARGS(SGameplayValidationIssueRow) {}
        SLATE_ARGUMENT(TSharedPtr<FGameplayValidationIssue>, Issue)
    SLATE_END_ARGS()

    void Construct(
        const FArguments& InArgs,
        const TSharedRef<STableViewBase>& OwnerTableView);

    virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
    FReply HandleFocusClicked();
    FText GetActorText() const;

    TSharedPtr<FGameplayValidationIssue> Issue;
};
