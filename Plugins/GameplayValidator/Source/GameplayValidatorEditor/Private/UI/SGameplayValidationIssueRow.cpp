#include "UI/SGameplayValidationIssueRow.h"

#include "GameFramework/Actor.h"
#include "Utils/GameplayValidationEditorUtils.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

void SGameplayValidationIssueRow::Construct(
    const FArguments& InArgs,
    const TSharedRef<STableViewBase>& OwnerTableView)
{
    Issue = InArgs._Issue;
    SMultiColumnTableRow<TSharedPtr<FGameplayValidationIssue>>::Construct(
        FSuperRowType::FArguments().Padding(2.0f),
        OwnerTableView);
}

TSharedRef<SWidget> SGameplayValidationIssueRow::GenerateWidgetForColumn(const FName& ColumnName)
{
    if (!Issue.IsValid())
    {
        return SNew(STextBlock).Text(FText::GetEmpty());
    }

    if (ColumnName == TEXT("Rule"))
    {
        return SNew(STextBlock).Text(FText::FromName(Issue->RuleId));
    }

    if (ColumnName == TEXT("Category"))
    {
        const FString Category = Issue->SlotId == TEXT("Default")
            ? Issue->ProviderId.ToString()
            : FString::Printf(TEXT("%s / %s"), *Issue->ProviderId.ToString(), *Issue->SlotId.ToString());
        return SNew(STextBlock).Text(FText::FromString(Category));
    }

    if (ColumnName == TEXT("Actor"))
    {
        return SNew(STextBlock).Text(GetActorText());
    }

    if (ColumnName == TEXT("Message"))
    {
        return SNew(STextBlock)
            .Text(Issue->Message)
            .AutoWrapText(true)
            .ToolTipText(Issue->Suggestion);
    }

    if (ColumnName == TEXT("Focus"))
    {
        return SNew(SButton)
            .Text(NSLOCTEXT("GameplayValidator", "FocusButton", "Focus"))
            .IsEnabled(Issue->TargetActor.IsValid())
            .OnClicked(this, &SGameplayValidationIssueRow::HandleFocusClicked);
    }

    return SNew(STextBlock).Text(FText::GetEmpty());
}

FReply SGameplayValidationIssueRow::HandleFocusClicked()
{
    FGameplayValidationEditorUtils::FocusActor(Issue.IsValid() ? Issue->TargetActor.Get() : nullptr);
    return FReply::Handled();
}

FText SGameplayValidationIssueRow::GetActorText() const
{
    if (!Issue.IsValid() || !Issue->TargetActor.IsValid())
    {
        return NSLOCTEXT("GameplayValidator", "ActorDeleted", "<Actor Deleted>");
    }

    return FText::FromString(Issue->TargetActor->GetActorLabel());
}
