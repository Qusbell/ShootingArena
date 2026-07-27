// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonAssetSyncEditor.h"

#include "JsonAssetSyncPackagingPreflight.h"
#include "JsonAssetSyncSettings.h"
#include "JsonAssetSyncSubsystem.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Logging/MessageLog.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "JsonAssetSyncEditor"

namespace JsonAssetSyncEditor::Private
{
	/**
	 * Message Log 내부에서 사용할 고유 목록 이름이다.
	 */
	const FName messageLogName(
		TEXT("JsonAssetSync")
	);

	/**
	 * 처리 대상 타입을 읽기 쉬운 문자열로 변환한다.
	 */
	const TCHAR* GetTargetTypeText(
		const EJsonApplyTargetType targetType
	)
	{
		switch (targetType)
		{
		case EJsonApplyTargetType::DataTable:
			return TEXT("DataTable");

		case EJsonApplyTargetType::DataAsset:
			return TEXT("DataAsset");

		default:
			return TEXT("Unknown");
		}
	}

	/**
	 * 처리 단계를 읽기 쉬운 문자열로 변환한다.
	 */
	const TCHAR* GetIssueStageText(
		const EJsonApplyIssueStage stage
	)
	{
		switch (stage)
		{
		case EJsonApplyIssueStage::Registry:
			return TEXT("Registry/Packaging");

		case EJsonApplyIssueStage::Path:
			return TEXT("Path");

		case EJsonApplyIssueStage::FileRead:
			return TEXT("File Read");

		case EJsonApplyIssueStage::JsonParse:
			return TEXT("JSON Parse");

		case EJsonApplyIssueStage::Structure:
			return TEXT("Structure");

		case EJsonApplyIssueStage::Conversion:
			return TEXT("Conversion");

		case EJsonApplyIssueStage::Commit:
			return TEXT("Commit");

		default:
			return TEXT("Unknown");
		}
	}

	/**
	 * 문제 구조체를 Message Log에 출력할 한 줄 문자열로 만든다.
	 */
	FText BuildIssueText(
		const FJsonApplyIssue& issue
	)
	{
		FString issueText =
			FString::Printf(
				TEXT(
					"[단계: %s] JSON/경로: %s | 대상: %s"
				),
				GetIssueStageText(issue.stage),
				issue.sourceJsonPath.IsEmpty()
					? TEXT("<없음>")
					: *issue.sourceJsonPath,
				issue.targetAssetPath.IsEmpty()
					? TEXT("<없음>")
					: *issue.targetAssetPath
			);

		if (!issue.rowName.IsNone())
		{
			issueText += FString::Printf(
				TEXT(" | Row: %s"),
				*issue.rowName.ToString()
			);
		}

		if (!issue.propertyPath.IsEmpty())
		{
			issueText += FString::Printf(
				TEXT(" | Field: %s"),
				*issue.propertyPath
			);
		}

		issueText += FString::Printf(
			TEXT(" | 내용: %s"),
			*issue.message
		);

		return FText::FromString(issueText);
	}

	/**
	 * 문제 심각도에 따라 Message Log에 기록한다.
	 */
	void AddIssueToMessageLog(
		FMessageLog& messageLog,
		const FJsonApplyIssue& issue
	)
	{
		const FText issueText =
			BuildIssueText(issue);

		switch (issue.severity)
		{
		case EJsonApplyIssueSeverity::Info:
			messageLog.Info(issueText);
			break;

		case EJsonApplyIssueSeverity::Warning:
			messageLog.Warning(issueText);
			break;

		case EJsonApplyIssueSeverity::Error:
		default:
			messageLog.Error(issueText);
			break;
		}
	}

	/**
	 * 패키징 사전검사 결과의 특정 심각도 문제 수를 계산한다.
	 */
	int32 CountReportIssues(
		const FJsonAssetSyncPackagingPreflightReport& report,
		const EJsonApplyIssueSeverity severity
	)
	{
		int32 count = 0;

		for (const FJsonApplyIssue& issue : report.issues)
		{
			if (issue.severity == severity)
			{
				++count;
			}
		}

		for (const FJsonApplyIssue& issue :
			report.dryRunSummary.globalIssues)
		{
			if (issue.severity == severity)
			{
				++count;
			}
		}

		for (const FJsonApplyResult& result :
			report.dryRunSummary.results)
		{
			for (const FJsonApplyIssue& issue : result.issues)
			{
				if (issue.severity == severity)
				{
					++count;
				}
			}
		}

		return count;
	}
}

void FJsonAssetSyncEditorModule::StartupModule()
{
	/*
	 * 메인 에디터 UI가 준비된 뒤 알림과 사전검사를 시작한다.
	 */
	editorInitializedHandle =
		FEditorDelegates::OnEditorInitialized.AddRaw(
			this,
			&FJsonAssetSyncEditorModule::
				HandleEditorInitialized
		);

	/*
	 * ToolMenus가 메뉴 등록 가능한 상태가 될 때까지 기다렸다가
	 * Tools 메뉴에 Apply JSON 항목 하나를 추가한다.
	 */
	toolMenusStartupCallbackHandle =
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(
				this,
				&FJsonAssetSyncEditorModule::
					RegisterToolMenus
			)
		);
}

void FJsonAssetSyncEditorModule::ShutdownModule()
{
	if (editorInitializedHandle.IsValid())
	{
		FEditorDelegates::OnEditorInitialized.Remove(
			editorInitializedHandle
		);

		editorInitializedHandle.Reset();
	}

	/*
	 * 메뉴 Delegate와 등록 항목을 먼저 제거한다.
	 */
	UnregisterToolMenus();

	UnbindFromRuntimeSubsystem();
	UnregisterMessageLog();
}

void FJsonAssetSyncEditorModule::HandleEditorInitialized(
	const double editorInitializationDuration
)
{
	static_cast<void>(editorInitializationDuration);

	if (editorInitializedHandle.IsValid())
	{
		FEditorDelegates::OnEditorInitialized.Remove(
			editorInitializedHandle
		);

		editorInitializedHandle.Reset();
	}

	RegisterMessageLog();
	BindToRuntimeSubsystem();
	RunPackagingPreflight();
}

void FJsonAssetSyncEditorModule::RegisterMessageLog()
{
	using namespace JsonAssetSyncEditor::Private;

	FMessageLogModule& messageLogModule =
		FModuleManager::LoadModuleChecked<
			FMessageLogModule
		>(
			TEXT("MessageLog")
		);

	if (messageLogModule.IsRegisteredLogListing(
		messageLogName
	))
	{
		return;
	}

	const FMessageLogInitializationOptions
		initializationOptions;

	messageLogModule.RegisterLogListing(
		messageLogName,
		LOCTEXT(
			"MessageLogLabel",
			"JSON Asset Sync"
		),
		initializationOptions
	);
}

void FJsonAssetSyncEditorModule::UnregisterMessageLog()
{
	using namespace JsonAssetSyncEditor::Private;

	if (!FModuleManager::Get().IsModuleLoaded(
		TEXT("MessageLog")
	))
	{
		return;
	}

	FMessageLogModule& messageLogModule =
		FModuleManager::GetModuleChecked<
			FMessageLogModule
		>(
			TEXT("MessageLog")
		);

	if (messageLogModule.IsRegisteredLogListing(
		messageLogName
	))
	{
		messageLogModule.UnregisterLogListing(
			messageLogName
		);
	}
}

void FJsonAssetSyncEditorModule::
	BindToRuntimeSubsystem()
{
	if (boundSubsystem.IsValid())
	{
		return;
	}

	if (!GEngine)
	{
		return;
	}

	UJsonAssetSyncSubsystem* subsystem =
		GEngine->GetEngineSubsystem<
			UJsonAssetSyncSubsystem
		>();

	if (!IsValid(subsystem))
	{
		return;
	}

	boundSubsystem = subsystem;

	processingCompletedHandle =
		subsystem->OnProcessingCompleted().AddRaw(
			this,
			&FJsonAssetSyncEditorModule::
				HandleProcessingCompleted
		);

	if (subsystem->HasCompletedInitialValidation())
	{
		const FJsonApplySummary summary =
			subsystem->GetLastSummary();

		if (HasMeaningfulSummary(summary))
		{
			HandleProcessingCompleted(summary);
		}
	}
}

void FJsonAssetSyncEditorModule::
	UnbindFromRuntimeSubsystem()
{
	if (!boundSubsystem.IsValid())
	{
		processingCompletedHandle.Reset();
		return;
	}

	if (processingCompletedHandle.IsValid())
	{
		boundSubsystem
			->OnProcessingCompleted()
			.Remove(processingCompletedHandle);

		processingCompletedHandle.Reset();
	}

	boundSubsystem.Reset();
}

void FJsonAssetSyncEditorModule::
	HandleProcessingCompleted(
		const FJsonApplySummary& summary
	)
{
	/*
	 * 수동 Apply JSON도 기존 자동 적용과 동일하게
	 * 전용 Message Log와 오른쪽 아래 알림을 사용한다.
	 */
	WriteSummaryToMessageLog(summary);

	const UJsonAssetSyncSettings* settings =
		GetDefault<UJsonAssetSyncSettings>();

	if (!IsValid(settings))
	{
		ShowSummaryNotification(summary);
		OpenMessageLog();
		return;
	}

	if (settings->showEditorNotifications)
	{
		ShowSummaryNotification(summary);
	}

	const int32 errorCount =
		CountIssues(
			summary,
			EJsonApplyIssueSeverity::Error
		);

	const bool hasFailure =
		!summary.isSystemReady ||
		summary.failureCount > 0 ||
		errorCount > 0;

	if (hasFailure &&
		settings->openMessageLogOnFailure)
	{
		OpenMessageLog();
	}
}

void FJsonAssetSyncEditorModule::
	WriteSummaryToMessageLog(
		const FJsonApplySummary& summary
	)
{
	using namespace JsonAssetSyncEditor::Private;

	FMessageLog messageLog(messageLogName);
	messageLog.SuppressLoggingToOutputLog(true);

	const FText pageTitle =
		FText::Format(
			LOCTEXT(
				"ResultPageTitle",
				"JSON 처리 결과 - 전체 {0}, 성공 {1}, 실패 {2}"
			),
			FText::AsNumber(summary.totalCount),
			FText::AsNumber(summary.successCount),
			FText::AsNumber(summary.failureCount)
		);

	messageLog.NewPage(pageTitle);

	for (const FJsonApplyIssue& globalIssue :
		summary.globalIssues)
	{
		AddIssueToMessageLog(
			messageLog,
			globalIssue
		);
	}

	for (const FJsonApplyResult& result :
		summary.results)
	{
		const FString targetTypeText =
			GetTargetTypeText(result.targetType);

		if (result.isSuccess)
		{
			const TCHAR* successText =
				result.wasApplied
					? TEXT("적용 성공")
					: TEXT("검사 성공");

			messageLog.Info(
				FText::FromString(
					FString::Printf(
						TEXT(
							"[%s][%s] JSON: %s | 대상: %s"
						),
						successText,
						*targetTypeText,
						*result.sourceJsonPath,
						*result.targetAssetPath
					)
				)
			);
		}
		else
		{
			messageLog.Error(
				FText::FromString(
					FString::Printf(
						TEXT(
							"[처리 실패][%s] JSON: %s | 대상: %s"
						),
						*targetTypeText,
						result.sourceJsonPath.IsEmpty()
							? TEXT("<없음>")
							: *result.sourceJsonPath,
						result.targetAssetPath.IsEmpty()
							? TEXT("<없음>")
							: *result.targetAssetPath
					)
				)
			);
		}

		for (const FJsonApplyIssue& issue :
			result.issues)
		{
			AddIssueToMessageLog(
				messageLog,
				issue
			);
		}
	}

	messageLog.Flush();
}

void FJsonAssetSyncEditorModule::
	ShowSummaryNotification(
		const FJsonApplySummary& summary
	)
{
	const int32 warningCount =
		CountIssues(
			summary,
			EJsonApplyIssueSeverity::Warning
		);

	const int32 errorCount =
		CountIssues(
			summary,
			EJsonApplyIssueSeverity::Error
		);

	const bool hasFailure =
		!summary.isSystemReady ||
		summary.failureCount > 0 ||
		errorCount > 0;

	FText notificationText;

	if (hasFailure)
	{
		notificationText =
			FText::Format(
				LOCTEXT(
					"FailureNotification",
					"JSON Asset Sync 완료\n성공 {0}개 / 실패 {1}개"
				),
				FText::AsNumber(summary.successCount),
				FText::AsNumber(summary.failureCount)
			);
	}
	else if (warningCount > 0)
	{
		notificationText =
			FText::Format(
				LOCTEXT(
					"WarningNotification",
					"JSON Asset Sync 완료\n성공 {0}개 / 실패 0개 / 경고 {1}개"
				),
				FText::AsNumber(summary.successCount),
				FText::AsNumber(warningCount)
			);
	}
	else
	{
		notificationText =
			FText::Format(
				LOCTEXT(
					"SuccessNotification",
					"JSON Asset Sync 완료\n성공 {0}개 / 실패 0개"
				),
				FText::AsNumber(summary.successCount)
			);
	}

	FNotificationInfo notificationInfo(
		notificationText
	);

	notificationInfo.bFireAndForget = true;
	notificationInfo.bUseSuccessFailIcons = true;
	notificationInfo.bUseLargeFont = false;
	notificationInfo.bUseThrobber = false;
	notificationInfo.ExpireDuration =
		hasFailure ? 12.0f : 8.0f;
	notificationInfo.FadeOutDuration = 0.5f;

	if (hasFailure || warningCount > 0)
	{
		notificationInfo.Hyperlink =
			FSimpleDelegate::CreateRaw(
				this,
				&FJsonAssetSyncEditorModule::
					OpenMessageLog
			);

		notificationInfo.HyperlinkText =
			LOCTEXT(
				"OpenDetailsHyperlink",
				"상세 보기"
			);
	}

	const TSharedPtr<SNotificationItem>
		notificationItem =
			FSlateNotificationManager::Get()
				.AddNotification(notificationInfo);

	if (!notificationItem.IsValid())
	{
		return;
	}

	notificationItem->SetCompletionState(
		hasFailure
			? SNotificationItem::CS_Fail
			: SNotificationItem::CS_Success
	);
}

void FJsonAssetSyncEditorModule::
	RunPackagingPreflight()
{
	const UJsonAssetSyncSettings* settings =
		GetDefault<UJsonAssetSyncSettings>();

	if (!IsValid(settings) ||
		!settings->runPackagingPreflightOnEditorStartup)
	{
		return;
	}

	const FJsonAssetSyncPackagingPreflightReport report =
		FJsonAssetSyncPackagingPreflight::Run(
			settings->autoConfigurePackagingSettings
		);

	WritePackagingPreflightToMessageLog(report);

	if (!report.isReady ||
		report.packagingSettingsChanged)
	{
		ShowPackagingPreflightNotification(report);
	}

	if (!report.isReady &&
		settings->openMessageLogOnFailure)
	{
		OpenMessageLog();
	}
}

void FJsonAssetSyncEditorModule::
	WritePackagingPreflightToMessageLog(
		const FJsonAssetSyncPackagingPreflightReport& report
	)
{
	using namespace JsonAssetSyncEditor::Private;

	FMessageLog messageLog(messageLogName);
	messageLog.SuppressLoggingToOutputLog(true);

	messageLog.NewPage(
		LOCTEXT(
			"PackagingPreflightPage",
			"JSON Asset Sync 패키징 사전검사"
		)
	);

	for (const FJsonApplyIssue& issue : report.issues)
	{
		AddIssueToMessageLog(
			messageLog,
			issue
		);
	}

	for (const FJsonApplyIssue& globalIssue :
		report.dryRunSummary.globalIssues)
	{
		AddIssueToMessageLog(
			messageLog,
			globalIssue
		);
	}

	for (const FJsonApplyResult& result :
		report.dryRunSummary.results)
	{
		if (result.isSuccess)
		{
			messageLog.Info(
				FText::FromString(
					FString::Printf(
						TEXT(
							"[Dry Run 성공][%s] JSON: %s | "
							"임시 대상: %s"
						),
						GetTargetTypeText(result.targetType),
						*result.sourceJsonPath,
						*result.targetAssetPath
					)
				)
			);
		}
		else
		{
			messageLog.Error(
				FText::FromString(
					FString::Printf(
						TEXT(
							"[Dry Run 실패][%s] JSON: %s | "
							"임시 대상: %s"
						),
						GetTargetTypeText(result.targetType),
						*result.sourceJsonPath,
						*result.targetAssetPath
					)
				)
			);
		}

		for (const FJsonApplyIssue& issue :
			result.issues)
		{
			AddIssueToMessageLog(
				messageLog,
				issue
			);
		}
	}

	const int32 warningCount =
		CountReportIssues(
			report,
			EJsonApplyIssueSeverity::Warning
		);

	const int32 errorCount =
		CountReportIssues(
			report,
			EJsonApplyIssueSeverity::Error
		);

	const FText summaryText =
		FText::Format(
			LOCTEXT(
				"PackagingPreflightSummary",
				"패키징 사전검사 완료 | 준비 상태: {0} | "
				"자동 설정 변경: {1} | Dry Run 성공: {2} | "
				"Dry Run 실패: {3} | 경고: {4} | 오류: {5}"
			),
			report.isReady
				? LOCTEXT("ReadyTrue", "정상")
				: LOCTEXT("ReadyFalse", "실패"),
			report.packagingSettingsChanged
				? LOCTEXT("ChangedTrue", "있음")
				: LOCTEXT("ChangedFalse", "없음"),
			FText::AsNumber(
				report.dryRunSummary.successCount
			),
			FText::AsNumber(
				report.dryRunSummary.failureCount
			),
			FText::AsNumber(warningCount),
			FText::AsNumber(errorCount)
		);

	if (report.isReady)
	{
		messageLog.Info(summaryText);
	}
	else
	{
		messageLog.Error(summaryText);
	}

	messageLog.Flush();
}

void FJsonAssetSyncEditorModule::
	ShowPackagingPreflightNotification(
		const FJsonAssetSyncPackagingPreflightReport& report
	)
{
	FText notificationText;

	if (!report.isReady)
	{
		notificationText =
			LOCTEXT(
				"PackagingPreflightFailed",
				"JSON Asset Sync 패키징 준비 실패\n"
				"상세 내용을 Message Log에서 확인하세요."
			);
	}
	else
	{
		notificationText =
			LOCTEXT(
				"PackagingPreflightFixed",
				"JSON Asset Sync 패키징 설정 보완 완료\n"
				"DefaultGame.ini에 필요한 경로를 추가했습니다."
			);
	}

	FNotificationInfo notificationInfo(
		notificationText
	);

	notificationInfo.bFireAndForget = true;
	notificationInfo.bUseSuccessFailIcons = true;
	notificationInfo.bUseLargeFont = false;
	notificationInfo.bUseThrobber = false;
	notificationInfo.ExpireDuration =
		report.isReady ? 8.0f : 12.0f;
	notificationInfo.FadeOutDuration = 0.5f;

	notificationInfo.Hyperlink =
		FSimpleDelegate::CreateRaw(
			this,
			&FJsonAssetSyncEditorModule::
				OpenMessageLog
		);

	notificationInfo.HyperlinkText =
		LOCTEXT(
			"OpenPackagingDetails",
			"상세 보기"
		);

	const TSharedPtr<SNotificationItem>
		notificationItem =
			FSlateNotificationManager::Get()
				.AddNotification(notificationInfo);

	if (!notificationItem.IsValid())
	{
		return;
	}

	notificationItem->SetCompletionState(
		report.isReady
			? SNotificationItem::CS_Success
			: SNotificationItem::CS_Fail
	);
}

void FJsonAssetSyncEditorModule::OpenMessageLog()
{
	using namespace JsonAssetSyncEditor::Private;

	FMessageLogModule& messageLogModule =
		FModuleManager::LoadModuleChecked<
			FMessageLogModule
		>(
			TEXT("MessageLog")
		);

	messageLogModule.OpenMessageLog(
		messageLogName
	);
}

void FJsonAssetSyncEditorModule::RegisterToolMenus()
{
	/*
	 * 이 Scope 안에서 등록되는 모든 메뉴 항목의 Owner를
	 * 현재 Editor 모듈 인스턴스로 지정한다.
	 *
	 * 모듈 종료 시 UToolMenus::UnregisterOwner(this)로
	 * 이 모듈이 등록한 항목만 안전하게 제거할 수 있다.
	 */
	FToolMenuOwnerScoped ownerScoped(this);

	UToolMenu* toolsMenu =
		UToolMenus::Get()->ExtendMenu(
			TEXT("LevelEditor.MainMenu.Tools")
		);

	if (!IsValid(toolsMenu))
	{
		return;
	}

	FToolMenuSection& section =
		toolsMenu->FindOrAddSection(
			TEXT("JsonAssetSync")
		);

	/*
	 * 사용자 요구에 따라 별도 서브메뉴나 검사 버튼을 만들지 않고,
	 * Apply JSON 항목 하나만 등록한다.
	 */
	section.AddMenuEntry(
		TEXT("JsonAssetSync.ApplyJson"),
		LOCTEXT(
			"ApplyJsonMenuLabel",
			"Apply JSON"
		),
		LOCTEXT(
			"ApplyJsonMenuTooltip",
			"Registry의 모든 JSON을 검사하고 정상 항목을 DataTable과 DataAsset에 적용합니다. 실패 이유는 JSON Asset Sync Message Log에 기록됩니다."
		),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateRaw(
				this,
				&FJsonAssetSyncEditorModule::
					ExecuteApplyJson
			),
			FCanExecuteAction::CreateRaw(
				this,
				&FJsonAssetSyncEditorModule::
					CanExecuteApplyJson
			)
		)
	);
}

void FJsonAssetSyncEditorModule::UnregisterToolMenus()
{
	/*
	 * 아직 실행되지 않은 ToolMenus 시작 콜백을 제거한다.
	 */
	if (toolMenusStartupCallbackHandle.IsValid())
	{
		UToolMenus::UnRegisterStartupCallback(
			toolMenusStartupCallbackHandle
		);

		toolMenusStartupCallbackHandle.Reset();
	}

	/*
	 * 이 모듈 인스턴스를 Owner로 등록한
	 * Apply JSON 메뉴 항목만 제거한다.
	 *
	 * ToolMenus가 이미 종료된 상황에서도 강제로 모듈을
	 * 다시 로드하지 않는 정적 API를 사용한다.
	 */
	UToolMenus::UnregisterOwner(this);
}

void FJsonAssetSyncEditorModule::ExecuteApplyJson()
{
	/*
	 * Runtime Subsystem을 통해 기존 검사·원자적 적용 로직을 실행한다.
	 *
	 * ApplyAllNow 내부에서 CompleteProcessing을 호출하므로,
	 * 처리 완료 이벤트를 통해 기존 알림과 Message Log가 자동 실행된다.
	 */
	if (!GEngine)
	{
		FJsonApplySummary failureSummary;

		FJsonApplyIssue issue;
		issue.stage = EJsonApplyIssueStage::Registry;
		issue.severity = EJsonApplyIssueSeverity::Error;
		issue.message =
			TEXT(
				"Apply JSON을 실행할 수 없습니다. "
				"GEngine이 유효하지 않습니다."
			);

		failureSummary.globalIssues.Add(MoveTemp(issue));

		HandleProcessingCompleted(failureSummary);
		return;
	}

	UJsonAssetSyncSubsystem* subsystem =
		GEngine->GetEngineSubsystem<
			UJsonAssetSyncSubsystem
		>();

	if (!IsValid(subsystem))
	{
		FJsonApplySummary failureSummary;

		FJsonApplyIssue issue;
		issue.stage = EJsonApplyIssueStage::Registry;
		issue.severity = EJsonApplyIssueSeverity::Error;
		issue.message =
			TEXT(
				"Apply JSON을 실행할 수 없습니다. "
				"JsonAssetSyncSubsystem을 가져오지 못했습니다."
			);

		failureSummary.globalIssues.Add(MoveTemp(issue));

		HandleProcessingCompleted(failureSummary);
		return;
	}

	subsystem->ApplyAllNow();
}

bool FJsonAssetSyncEditorModule::
	CanExecuteApplyJson() const
{
	/*
	 * 플레이 중 DataTable Row 메모리나 DataAsset 값을 교체하면
	 * 이미 실행 중인 게임 로직과 캐시가 불일치할 수 있다.
	 *
	 * 따라서 에디터가 플레이 중이 아닐 때만 적용할 수 있게 한다.
	 */
	if (GEditor != nullptr &&
		GEditor->PlayWorld != nullptr)
	{
		return false;
	}

	return GEngine != nullptr;
}

int32 FJsonAssetSyncEditorModule::CountIssues(
	const FJsonApplySummary& summary,
	const EJsonApplyIssueSeverity severity
) const
{
	int32 issueCount = 0;

	for (const FJsonApplyIssue& globalIssue :
		summary.globalIssues)
	{
		if (globalIssue.severity == severity)
		{
			++issueCount;
		}
	}

	for (const FJsonApplyResult& result :
		summary.results)
	{
		for (const FJsonApplyIssue& issue :
			result.issues)
		{
			if (issue.severity == severity)
			{
				++issueCount;
			}
		}
	}

	return issueCount;
}

bool FJsonAssetSyncEditorModule::HasMeaningfulSummary(
	const FJsonApplySummary& summary
) const
{
	return summary.isSystemReady ||
		summary.totalCount > 0 ||
		!summary.globalIssues.IsEmpty() ||
		!summary.results.IsEmpty();
}

IMPLEMENT_MODULE(
	FJsonAssetSyncEditorModule,
	JsonAssetSyncEditor
)

#undef LOCTEXT_NAMESPACE
