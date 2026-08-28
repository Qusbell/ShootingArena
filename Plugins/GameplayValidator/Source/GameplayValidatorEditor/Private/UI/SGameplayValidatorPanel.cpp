#include "UI/SGameplayValidatorPanel.h"

#include "GameFramework/Actor.h"
#include "GameplayValidationSubsystem.h"
#include "GameplayValidatorSettings.h"
#include "UI/SGameplayValidationIssueRow.h"
#include "Editor.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "GameplayValidatorPanel"

void SGameplayValidatorPanel::Construct(const FArguments& InArgs)
{
    RebuildFilterOptions();

    SeverityOptions = {
        MakeShared<EGameplayValidatorSeverityFilter>(EGameplayValidatorSeverityFilter::All),
        MakeShared<EGameplayValidatorSeverityFilter>(EGameplayValidatorSeverityFilter::Error),
        MakeShared<EGameplayValidatorSeverityFilter>(EGameplayValidatorSeverityFilter::Warning)
    };
    SelectedSeverity = SeverityOptions[0];

    if (UGameplayValidationSubsystem* Subsystem = GetSubsystem())
    {
        ReportChangedHandle = Subsystem->OnReportChanged().AddRaw(this, &SGameplayValidatorPanel::HandleReportChanged);
    }

    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.0f)
            [
                SNew(SExpandableArea)
                .InitiallyCollapsed(false)
                .HeaderContent()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ValidationTargets", "Validation Targets"))
                ]
                .BodyContent()
                [
                    BuildTargetSettings()
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.0f, 2.0f)
            [
                SNew(SButton)
                .Text(LOCTEXT("ValidateCurrentLevel", "Validate Current Level"))
                .HAlign(HAlign_Center)
                .OnClicked(this, &SGameplayValidatorPanel::HandleValidateClicked)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.0f)
            [
                BuildFilterBar()
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.0f)
            [
                SAssignNew(ResultHost, SBox)
                [
                    BuildResultsWidget()
                ]
            ]
        ]
    ];

    // 탭을 닫았다 다시 열어도 Subsystem이 보관 중인 마지막 검사 결과를 즉시 복원합니다.
    RefreshFilteredIssues();
}

SGameplayValidatorPanel::~SGameplayValidatorPanel()
{
    if (UGameplayValidationSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->OnReportChanged().Remove(ReportChangedHandle);
    }
}

UGameplayValidationSubsystem* SGameplayValidatorPanel::GetSubsystem() const
{
    return GEditor ? GEditor->GetEditorSubsystem<UGameplayValidationSubsystem>() : nullptr;
}

TSharedRef<SWidget> SGameplayValidatorPanel::BuildTargetSettings()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    if (UGameplayValidationSubsystem* Subsystem = GetSubsystem())
    {
        for (const TSharedRef<IGameplayValidationProvider>& Provider : Subsystem->GetProviderRegistry().GetProviders())
        {
            Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(Provider->GetDisplayName())
            ];

            TArray<FGameplayValidationTargetSlot> Slots;
            Provider->GetTargetSlots(Slots);
            for (const FGameplayValidationTargetSlot& Slot : Slots)
            {
                Box->AddSlot()
                .AutoHeight()
                .Padding(8.0f, 2.0f)
                [
                    BuildTargetSlotRow(Provider->GetProviderId(), Slot.SlotId, Slot.DisplayName)
                ];
            }
        }
    }

    return Box;
}

TSharedRef<SWidget> SGameplayValidatorPanel::BuildTargetSlotRow(
    const FName ProviderId,
    const FName SlotId,
    const FText& Label)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 8.0f, 0.0f)
        [
            SNew(SBox)
            .WidthOverride(140.0f)
            [
                SNew(STextBlock).Text(Label)
            ]
        ]
        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        [
            SNew(SClassPropertyEntryBox)
            .MetaClass(AActor::StaticClass())
            .AllowNone(true)
            .SelectedClass_Lambda([this, ProviderId, SlotId]() -> const UClass*
            {
                return GetTargetClass(ProviderId, SlotId);
            })
            .OnSetClass_Lambda([this, ProviderId, SlotId](const UClass* NewClass)
            {
                HandleTargetClassSet(ProviderId, SlotId, NewClass);
            })
        ];
}

TSharedRef<SWidget> SGameplayValidatorPanel::BuildFilterBar()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .FillWidth(0.30f)
        .Padding(0.0f, 0.0f, 6.0f, 0.0f)
        [
            SNew(SComboBox<TSharedPtr<FGameplayValidatorCategoryOption>>)
            .OptionsSource(&CategoryOptions)
            .InitiallySelectedItem(SelectedCategory)
            .OnGenerateWidget_Lambda([](TSharedPtr<FGameplayValidatorCategoryOption> Option)
            {
                return SNew(STextBlock).Text(Option.IsValid() ? Option->Label : FText::GetEmpty());
            })
            .OnSelectionChanged_Lambda([this](TSharedPtr<FGameplayValidatorCategoryOption> NewValue, ESelectInfo::Type)
            {
                SelectedCategory = NewValue;
                RefreshFilteredIssues();
            })
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    return SelectedCategory.IsValid() ? SelectedCategory->Label : LOCTEXT("AllCategory", "All");
                })
            ]
        ]

        + SHorizontalBox::Slot()
        .FillWidth(0.20f)
        .Padding(0.0f, 0.0f, 6.0f, 0.0f)
        [
            SNew(SComboBox<TSharedPtr<EGameplayValidatorSeverityFilter>>)
            .OptionsSource(&SeverityOptions)
            .InitiallySelectedItem(SelectedSeverity)
            .OnGenerateWidget_Lambda([](TSharedPtr<EGameplayValidatorSeverityFilter> Option)
            {
                FText Text = LOCTEXT("AllSeverity", "All");
                if (Option.IsValid() && *Option == EGameplayValidatorSeverityFilter::Error)
                {
                    Text = LOCTEXT("ErrorSeverity", "Error");
                }
                else if (Option.IsValid() && *Option == EGameplayValidatorSeverityFilter::Warning)
                {
                    Text = LOCTEXT("WarningSeverity", "Warning");
                }
                return SNew(STextBlock).Text(Text);
            })
            .OnSelectionChanged_Lambda([this](TSharedPtr<EGameplayValidatorSeverityFilter> NewValue, ESelectInfo::Type)
            {
                SelectedSeverity = NewValue;
                RefreshFilteredIssues();
            })
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    if (!SelectedSeverity.IsValid() || *SelectedSeverity == EGameplayValidatorSeverityFilter::All)
                    {
                        return LOCTEXT("AllSeverityCurrent", "All");
                    }
                    return *SelectedSeverity == EGameplayValidatorSeverityFilter::Error
                        ? LOCTEXT("ErrorSeverityCurrent", "Error")
                        : LOCTEXT("WarningSeverityCurrent", "Warning");
                })
            ]
        ]

        + SHorizontalBox::Slot()
        .FillWidth(0.40f)
        .Padding(0.0f, 0.0f, 6.0f, 0.0f)
        [
            SAssignNew(SearchBox, SSearchBox)
            .HintText(LOCTEXT("SearchHint", "Actor / Rule ID / Message 검색"))
            .OnTextChanged(this, &SGameplayValidatorPanel::HandleSearchChanged)
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            SNew(SButton)
            .Text(LOCTEXT("ResetFilters", "Reset"))
            .OnClicked(this, &SGameplayValidatorPanel::HandleResetFiltersClicked)
        ];
}

TSharedRef<SWidget> SGameplayValidatorPanel::BuildSummaryWidget() const
{
    const UGameplayValidationSubsystem* Subsystem = GetSubsystem();
    if (!Subsystem)
    {
        return SNew(STextBlock).Text(LOCTEXT("SubsystemMissing", "Gameplay Validation Subsystem을 찾을 수 없습니다."));
    }

    const FGameplayValidationReport& Report = Subsystem->GetLastReport();
    if (!Report.bHasRun)
    {
        return SNew(STextBlock).Text(LOCTEXT("NotValidated", "현재 레벨은 아직 검사하지 않았습니다."));
    }

    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    Box->AddSlot().AutoHeight()
    [
        SNew(STextBlock)
        .Text(FText::Format(
            LOCTEXT("SummaryHeader", "Level: {0}   |   Total Checked Actors: {1}"),
            FText::FromString(Report.LevelName),
            FText::AsNumber(Report.TotalCheckedActors)))
    ];

    for (const FGameplayValidationTargetCount& Count : Report.TargetCounts)
    {
        const FText Name = Count.Key.SlotId == TEXT("Default")
            ? Count.ProviderDisplayName
            : FText::Format(LOCTEXT("ProviderSlotName", "{0} / {1}"), Count.ProviderDisplayName, Count.SlotDisplayName);

        Box->AddSlot().AutoHeight().Padding(8.0f, 1.0f)
        [
            SNew(STextBlock)
            .Text(FText::Format(LOCTEXT("TargetCount", "{0}: {1}"), Name, FText::AsNumber(Count.Count)))
        ];
    }

    for (const FGameplayValidationMetric& Metric : Report.Metrics)
    {
        Box->AddSlot().AutoHeight().Padding(16.0f, 1.0f)
        [
            SNew(STextBlock)
            .Text(FText::Format(
                LOCTEXT("MetricCount", "{0} - {1}: {2}"),
                FText::FromName(Metric.ProviderId),
                Metric.DisplayName,
                FText::AsNumber(Metric.Value)))
        ];
    }

    return Box;
}

TSharedRef<SWidget> SGameplayValidatorPanel::BuildResultsWidget()
{
    const UGameplayValidationSubsystem* Subsystem = GetSubsystem();
    if (!Subsystem)
    {
        return SNew(STextBlock).Text(LOCTEXT("NoSubsystem", "Subsystem unavailable."));
    }

    const FGameplayValidationReport& Report = Subsystem->GetLastReport();

    TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);
    Root->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
    [
        BuildSummaryWidget()
    ];

    for (const FGameplayValidationScanMessage& Message : Subsystem->GetConfigurationMessages())
    {
        Root->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
        [
            SNew(STextBlock)
            .Text(Message.Message)
            .AutoWrapText(true)
        ];
    }

    if (!Report.bHasRun)
    {
        return Root;
    }

    if (Report.IsSuccess() && Subsystem->GetConfigurationMessages().IsEmpty())
    {
        Root->AddSlot().AutoHeight().Padding(0.0f, 8.0f)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("ValidationSuccess", "Success - 현재 레벨에서 Error / Warning을 발견하지 못했습니다."))
            .AutoWrapText(true)
        ];
        return Root;
    }

    if (Report.IsSuccess() && !Subsystem->GetConfigurationMessages().IsEmpty())
    {
        Root->AddSlot().AutoHeight().Padding(0.0f, 8.0f)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("ValidationIncomplete", "일부 검사 대상 Blueprint가 등록되지 않아 전체 Success로 판정하지 않았습니다."))
            .AutoWrapText(true)
        ];
        return Root;
    }

    if (ErrorItems.Num() > 0)
    {
        Root->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
        [
            BuildIssueSection(EGameplayValidationSeverity::Error, ErrorItems)
        ];
    }

    if (WarningItems.Num() > 0)
    {
        Root->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
        [
            BuildIssueSection(EGameplayValidationSeverity::Warning, WarningItems)
        ];
    }

    if (ErrorItems.IsEmpty() && WarningItems.IsEmpty())
    {
        Root->AddSlot().AutoHeight().Padding(0.0f, 8.0f)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("NoFilteredResults", "현재 Filter 조건에 표시할 결과가 없습니다."))
        ];
    }

    return Root;
}

TSharedRef<SWidget> SGameplayValidatorPanel::BuildIssueSection(
    const EGameplayValidationSeverity Severity,
    const TArray<TSharedPtr<FGameplayValidationIssue>>& Items)
{
    const bool bError = Severity == EGameplayValidationSeverity::Error;

    int32 TotalSeverityCount = Items.Num();
    if (const UGameplayValidationSubsystem* Subsystem = GetSubsystem())
    {
        TotalSeverityCount = Subsystem->GetLastReport().GetIssueCount(Severity);
    }

    const FText Header = Items.Num() == TotalSeverityCount
        ? FText::Format(
            bError ? LOCTEXT("ErrorHeader", "Error ({0})") : LOCTEXT("WarningHeader", "Warning ({0})"),
            FText::AsNumber(Items.Num()))
        : FText::Format(
            bError ? LOCTEXT("FilteredErrorHeader", "Error ({0} / {1})") : LOCTEXT("FilteredWarningHeader", "Warning ({0} / {1})"),
            FText::AsNumber(Items.Num()),
            FText::AsNumber(TotalSeverityCount));

    TSharedRef<SListView<TSharedPtr<FGameplayValidationIssue>>> List =
        SNew(SListView<TSharedPtr<FGameplayValidationIssue>>)
        .ListItemsSource(bError ? &ErrorItems : &WarningItems)
        .OnGenerateRow(this, &SGameplayValidatorPanel::GenerateIssueRow)
        .HeaderRow
        (
            SNew(SHeaderRow)
            + SHeaderRow::Column(TEXT("Rule")).DefaultLabel(LOCTEXT("RuleColumn", "Rule")).FillWidth(0.14f)
            + SHeaderRow::Column(TEXT("Category")).DefaultLabel(LOCTEXT("CategoryColumn", "Category")).FillWidth(0.14f)
            + SHeaderRow::Column(TEXT("Actor")).DefaultLabel(LOCTEXT("ActorColumn", "Actor")).FillWidth(0.20f)
            + SHeaderRow::Column(TEXT("Message")).DefaultLabel(LOCTEXT("MessageColumn", "Message")).FillWidth(0.44f)
            + SHeaderRow::Column(TEXT("Focus")).DefaultLabel(FText::GetEmpty()).FixedWidth(70.0f)
        );

    return SNew(SExpandableArea)
        .InitiallyCollapsed(!bError)
        .HeaderContent()
        [
            SNew(STextBlock).Text(Header)
        ]
        .BodyContent()
        [
            SNew(SBox)
            .HeightOverride(FMath::Clamp(48.0f + Items.Num() * 32.0f, 96.0f, 360.0f))
            [
                List
            ]
        ];
}

FReply SGameplayValidatorPanel::HandleValidateClicked()
{
    if (UGameplayValidationSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->ValidateCurrentLevel();
    }
    return FReply::Handled();
}

FReply SGameplayValidatorPanel::HandleResetFiltersClicked()
{
    SelectedCategory = CategoryOptions.Num() > 0 ? CategoryOptions[0] : nullptr;
    SelectedSeverity = SeverityOptions.Num() > 0 ? SeverityOptions[0] : nullptr;
    SearchText.Reset();
    if (SearchBox.IsValid())
    {
        SearchBox->SetText(FText::GetEmpty());
    }
    RefreshFilteredIssues();
    return FReply::Handled();
}

void SGameplayValidatorPanel::HandleReportChanged()
{
    RefreshFilteredIssues();
}

void SGameplayValidatorPanel::HandleSearchChanged(const FText& NewText)
{
    SearchText = NewText.ToString();
    RefreshFilteredIssues();
}

void SGameplayValidatorPanel::HandleTargetClassSet(
    const FName ProviderId,
    const FName SlotId,
    const UClass* NewClass)
{
    UGameplayValidatorSettings* Settings = GetMutableDefault<UGameplayValidatorSettings>();
    Settings->SetTargetClass(ProviderId, SlotId, const_cast<UClass*>(NewClass));

    if (UGameplayValidationSubsystem* Subsystem = GetSubsystem())
    {
        Subsystem->ClearReport();
    }
}

UClass* SGameplayValidatorPanel::GetTargetClass(const FName ProviderId, const FName SlotId) const
{
    return GetDefault<UGameplayValidatorSettings>()->LoadTargetClass(ProviderId, SlotId);
}

void SGameplayValidatorPanel::RebuildFilterOptions()
{
    CategoryOptions.Reset();

    TSharedPtr<FGameplayValidatorCategoryOption> All = MakeShared<FGameplayValidatorCategoryOption>();
    All->Label = LOCTEXT("AllFilter", "All");
    All->bAll = true;
    CategoryOptions.Add(All);
    SelectedCategory = All;

    if (UGameplayValidationSubsystem* Subsystem = GetSubsystem())
    {
        for (const TSharedRef<IGameplayValidationProvider>& Provider : Subsystem->GetProviderRegistry().GetProviders())
        {
            TArray<FGameplayValidationTargetSlot> Slots;
            Provider->GetTargetSlots(Slots);

            if (Slots.Num() > 1)
            {
                TSharedPtr<FGameplayValidatorCategoryOption> ProviderOption = MakeShared<FGameplayValidatorCategoryOption>();
                ProviderOption->Label = Provider->GetDisplayName();
                ProviderOption->ProviderId = Provider->GetProviderId();
                ProviderOption->bProviderOnly = true;
                CategoryOptions.Add(ProviderOption);
            }

            for (const FGameplayValidationTargetSlot& Slot : Slots)
            {
                TSharedPtr<FGameplayValidatorCategoryOption> SlotOption = MakeShared<FGameplayValidatorCategoryOption>();
                SlotOption->ProviderId = Provider->GetProviderId();
                SlotOption->SlotId = Slot.SlotId;
                SlotOption->Label = Slots.Num() > 1
                    ? FText::Format(LOCTEXT("FilterProviderSlot", "{0} / {1}"), Provider->GetDisplayName(), Slot.DisplayName)
                    : Provider->GetDisplayName();
                CategoryOptions.Add(SlotOption);
            }
        }
    }
}

void SGameplayValidatorPanel::RefreshFilteredIssues()
{
    ErrorItems.Reset();
    WarningItems.Reset();

    if (const UGameplayValidationSubsystem* Subsystem = GetSubsystem())
    {
        const FGameplayValidationReport& Report = Subsystem->GetLastReport();
        for (const FGameplayValidationIssue& Issue : Report.Issues)
        {
            if (!PassesFilters(Issue))
            {
                continue;
            }

            TSharedPtr<FGameplayValidationIssue> SharedIssue = MakeShared<FGameplayValidationIssue>(Issue);
            if (Issue.Severity == EGameplayValidationSeverity::Error)
            {
                ErrorItems.Add(SharedIssue);
            }
            else
            {
                WarningItems.Add(SharedIssue);
            }
        }
    }

    if (ResultHost.IsValid())
    {
        ResultHost->SetContent(BuildResultsWidget());
    }
}

bool SGameplayValidatorPanel::PassesFilters(const FGameplayValidationIssue& Issue) const
{
    if (SelectedCategory.IsValid() && !SelectedCategory->bAll)
    {
        if (Issue.ProviderId != SelectedCategory->ProviderId)
        {
            return false;
        }

        if (!SelectedCategory->bProviderOnly
            && !SelectedCategory->SlotId.IsNone()
            && Issue.SlotId != SelectedCategory->SlotId)
        {
            return false;
        }
    }

    if (SelectedSeverity.IsValid())
    {
        if (*SelectedSeverity == EGameplayValidatorSeverityFilter::Error
            && Issue.Severity != EGameplayValidationSeverity::Error)
        {
            return false;
        }
        if (*SelectedSeverity == EGameplayValidatorSeverityFilter::Warning
            && Issue.Severity != EGameplayValidationSeverity::Warning)
        {
            return false;
        }
    }

    if (!SearchText.IsEmpty())
    {
        const FString ActorName = Issue.TargetActor.IsValid() ? Issue.TargetActor->GetActorLabel() : FString();
        const FString Haystack = FString::Printf(
            TEXT("%s %s %s %s %s"),
            *Issue.RuleId.ToString(),
            *Issue.ProviderId.ToString(),
            *Issue.SlotId.ToString(),
            *ActorName,
            *Issue.Message.ToString());

        if (!Haystack.Contains(SearchText, ESearchCase::IgnoreCase))
        {
            return false;
        }
    }

    return true;
}

TSharedRef<ITableRow> SGameplayValidatorPanel::GenerateIssueRow(
    TSharedPtr<FGameplayValidationIssue> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(SGameplayValidationIssueRow, OwnerTable)
        .Issue(Item);
}

#undef LOCTEXT_NAMESPACE
