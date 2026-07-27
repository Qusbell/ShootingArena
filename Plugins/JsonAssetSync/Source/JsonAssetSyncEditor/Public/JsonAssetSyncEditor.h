// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JsonApplyTypes.h"
#include "Modules/ModuleManager.h"

class UJsonAssetSyncSubsystem;
struct FJsonAssetSyncPackagingPreflightReport;

/**
 * JSON Asset Sync의 에디터 전용 기능을 담당하는 모듈이다.
 *
 * 주요 기능:
 *
 * 1. 에디터 초기화가 완전히 끝날 때까지 대기
 * 2. JSON Asset Sync 전용 Message Log 등록
 * 3. Runtime Subsystem의 처리 완료 이벤트 구독
 * 4. 성공·경고·실패 알림 표시
 * 5. 실패 시 전용 Message Log 자동 열기
 * 6. 패키징 준비 상태 자동 사전검사
 * 7. Tools 메뉴에 Apply JSON 항목 하나만 제공
 */
class FJsonAssetSyncEditorModule final :
	public IModuleInterface
{
public:
	/**
	 * Editor 모듈이 로드될 때 호출된다.
	 */
	virtual void StartupModule() override;

	/**
	 * Editor 모듈이 종료될 때 호출된다.
	 */
	virtual void ShutdownModule() override;

private:
	/**
	 * 에디터 초기화가 완전히 끝났을 때 호출된다.
	 */
	void HandleEditorInitialized(
		double editorInitializationDuration
	);

	/**
	 * JSON Asset Sync 전용 Message Log 목록을 등록한다.
	 */
	void RegisterMessageLog();

	/**
	 * 등록한 Message Log 목록을 해제한다.
	 */
	void UnregisterMessageLog();

	/**
	 * Runtime JSON Asset Sync Subsystem의
	 * 처리 완료 이벤트에 연결한다.
	 */
	void BindToRuntimeSubsystem();

	/**
	 * Runtime Subsystem에 등록한 이벤트 연결을 제거한다.
	 */
	void UnbindFromRuntimeSubsystem();

	/**
	 * JSON 검사 또는 적용이 끝났을 때 호출된다.
	 */
	void HandleProcessingCompleted(
		const FJsonApplySummary& summary
	);

	/**
	 * 전체 JSON 처리 결과를 전용 Message Log에 기록한다.
	 */
	void WriteSummaryToMessageLog(
		const FJsonApplySummary& summary
	);

	/**
	 * JSON 처리 결과를 에디터 오른쪽 아래 알림으로 표시한다.
	 */
	void ShowSummaryNotification(
		const FJsonApplySummary& summary
	);

	/**
	 * 패키징 준비 상태를 검사하고 결과를 기록한다.
	 */
	void RunPackagingPreflight();

	/**
	 * 패키징 사전검사 결과를 Message Log에 기록한다.
	 */
	void WritePackagingPreflightToMessageLog(
		const FJsonAssetSyncPackagingPreflightReport& report
	);

	/**
	 * 패키징 사전검사 실패 또는 자동 수정 결과를 알림으로 표시한다.
	 */
	void ShowPackagingPreflightNotification(
		const FJsonAssetSyncPackagingPreflightReport& report
	);

	/**
	 * JSON Asset Sync 전용 Message Log를 연다.
	 */
	void OpenMessageLog();

	/**
	 * Tools 메뉴에 Apply JSON 항목 하나를 등록한다.
	 */
	void RegisterToolMenus();

	/**
	 * Tools 메뉴에 등록한 항목과 시작 콜백을 해제한다.
	 */
	void UnregisterToolMenus();

	/**
	 * Tools → Apply JSON을 눌렀을 때 호출된다.
	 *
	 * 기존 Runtime Subsystem의 ApplyAllNow를 호출하므로
	 * 검사, 적용, 알림, Message Log 처리를 그대로 재사용한다.
	 */
	void ExecuteApplyJson();

	/**
	 * Apply JSON 메뉴를 현재 실행할 수 있는지 확인한다.
	 *
	 * 플레이 중 DataTable Row 포인터나 게임 상태가 바뀌는 것을 막기 위해
	 * PIE 또는 Standalone 실행 중에는 비활성화한다.
	 */
	bool CanExecuteApplyJson() const;

	/**
	 * 전체 처리 결과에서 특정 심각도의 문제 개수를 계산한다.
	 */
	int32 CountIssues(
		const FJsonApplySummary& summary,
		EJsonApplyIssueSeverity severity
	) const;

	/**
	 * 실제 JSON 처리 결과가 만들어진 Summary인지 확인한다.
	 */
	bool HasMeaningfulSummary(
		const FJsonApplySummary& summary
	) const;

private:
	/**
	 * 에디터 초기화 완료 Delegate에 등록한 Handle이다.
	 */
	FDelegateHandle editorInitializedHandle;

	/**
	 * ToolMenus의 안전한 메뉴 등록 콜백 Handle이다.
	 */
	FDelegateHandle toolMenusStartupCallbackHandle;

	/**
	 * 현재 처리 완료 이벤트를 구독한 Runtime Subsystem이다.
	 */
	TWeakObjectPtr<UJsonAssetSyncSubsystem>
		boundSubsystem;

	/**
	 * Runtime Subsystem의 처리 완료 Delegate Handle이다.
	 */
	FDelegateHandle processingCompletedHandle;
};
