// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonAssetSyncSubsystem.h"

#include "JsonApplyRegistry.h"
#include "JsonApplyService.h"
#include "JsonAssetSyncLog.h"
#include "JsonAssetSyncSettings.h"

#include "Misc/CoreDelegates.h"

void UJsonAssetSyncSubsystem::Initialize(
	FSubsystemCollectionBase& collection
)
{
	Super::Initialize(collection);

	/*
	 * Engine Subsystem 초기화 직후에는 엔진 전체 초기화가
	 * 아직 완료되지 않았을 수 있다.
	 *
	 * 실제 JSON 처리는 엔진 초기화 완료 Delegate에서 실행한다.
	 */
	engineLoopInitCompleteHandle =
		FCoreDelegates::OnFEngineLoopInitComplete.AddUObject(
			this,
			&UJsonAssetSyncSubsystem::
			HandleEngineLoopInitComplete
		);
}

void UJsonAssetSyncSubsystem::Deinitialize()
{
	/*
	 * 종료 과정에서 이미 해제된 Subsystem이 호출되지 않도록
	 * 엔진 초기화 완료 Delegate를 제거한다.
	 */
	if (engineLoopInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(
			engineLoopInitCompleteHandle
		);

		engineLoopInitCompleteHandle.Reset();
	}

	/*
	 * 에디터 모듈 등 외부 구독자를 정리한다.
	 */
	processingCompletedEvent.Clear();

	Super::Deinitialize();
}

void UJsonAssetSyncSubsystem::
HandleEngineLoopInitComplete()
{
	const UJsonAssetSyncSettings* settings =
		GetDefault<UJsonAssetSyncSettings>();

	if (!IsValid(settings))
	{
		const FJsonApplySummary summary =
			RunProcessing(true);

		initialValidationCompleted = true;

		CompleteProcessing(summary);
		return;
	}

	bool shouldRunAutomatically = false;

#if WITH_EDITOR
	if (GIsEditor)
	{
		/*
		 * Unreal Editor에서는 에디터 자동 적용 설정을 사용한다.
		 */
		shouldRunAutomatically =
			settings->applyOnEditorStartup;

		/*
		 * Apply And Save는 엔진 초기화 완료 시점에 바로 실행하면
		 * Blueprint Validator 등 에디터 검증기가 아직 등록 중일 수 있다.
		 *
		 * 따라서 이 모드만 Editor 모듈의 OnEditorInitialized 이후
		 * 다음 Tick으로 넘겨서 적용·저장한다.
		 *
		 * Memory Only는 디스크 저장을 하지 않으므로 기존처럼
		 * 이 시점에 바로 적용해도 된다.
		 */
		if (shouldRunAutomatically &&
			settings->editorApplyMode ==
				EJsonEditorApplyMode::ApplyAndSave)
		{
			UE_LOG(
				LogJsonAssetSync,
				Display,
				TEXT(
					"Apply And Save 자동 적용을 "
					"에디터 초기화 이후로 지연합니다."
				)
			);

			return;
		}
	}
	else
#endif
	{
		/*
		 * 패키징 게임에서는 런타임 자동 적용 설정을 사용한다.
		 */
		shouldRunAutomatically =
			settings->applyOnRuntimeStartup;
	}

	if (!shouldRunAutomatically)
	{
		UE_LOG(
			LogJsonAssetSync,
			Display,
			TEXT(
				"시작 시 JSON 자동 적용이 "
				"Project Settings에서 비활성화되어 있습니다."
			)
		);

		initialValidationCompleted = true;
		return;
	}

	/*
	 * 시작 시 Registry 전체를 검사하고 실제로 적용한다.
	 */
	const FJsonApplySummary summary =
		RunProcessing(true);

	initialValidationCompleted = true;

	CompleteProcessing(summary);
}

FJsonApplySummary
UJsonAssetSyncSubsystem::ValidateAllNow()
{
	/*
	 * 실제 에셋은 변경하지 않고 검사만 수행한다.
	 */
	const FJsonApplySummary summary =
		RunProcessing(false);

	initialValidationCompleted = true;

	CompleteProcessing(summary);

	return summary;
}

FJsonApplySummary
UJsonAssetSyncSubsystem::ApplyAllNow()
{
	/*
	 * Registry 전체를 검사하고 실제 대상 에셋에 적용한다.
	 */
	const FJsonApplySummary summary =
		RunProcessing(true);

	initialValidationCompleted = true;

	CompleteProcessing(summary);

	return summary;
}

FJsonApplySummary
UJsonAssetSyncSubsystem::RunProcessing(
	const bool applyChanges
)
{
	const UJsonAssetSyncSettings* settings =
		GetDefault<UJsonAssetSyncSettings>();

	UJsonApplyRegistry* registry = nullptr;

	if (IsValid(settings) &&
		!settings->registryAsset.IsNull())
	{
		/*
		 * Registry는 Soft Object Reference이므로
		 * 실제 처리 시점에 동기적으로 로드한다.
		 */
		registry =
			settings->registryAsset.LoadSynchronous();
	}

	const FJsonApplySummary summary =
		applyChanges
		? FJsonApplyService::ApplyAll(
			registry,
			settings
		)
		: FJsonApplyService::ValidateAll(
			registry,
			settings
		);

	const bool logSuccessfulApplications =
		IsValid(settings)
		? settings->logSuccessfulApplications
		: true;

	FJsonApplyService::WriteSummaryToLog(
		summary,
		logSuccessfulApplications
	);

	return summary;
}

void UJsonAssetSyncSubsystem::CompleteProcessing(
	const FJsonApplySummary& summary
)
{
	/*
	 * 가장 최근 결과를 Subsystem에 저장한다.
	 */
	lastSummary = summary;

	/*
	 * 에디터 알림 모듈 등 현재 구독 중인 객체에
	 * JSON 처리가 완료됐음을 알린다.
	 */
	processingCompletedEvent.Broadcast(lastSummary);
}