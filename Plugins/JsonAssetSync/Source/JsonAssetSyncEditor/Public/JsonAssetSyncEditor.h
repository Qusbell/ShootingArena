// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
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
	 * Apply And Save의 시작 자동 적용을 다음 Editor Tick으로 예약한다.
	 *
	 * OnEditorInitialized Delegate의 다른 구독자들이 모두 실행된 뒤
	 * 저장하도록 하여 Blueprint Validator 초기화 경고를 방지한다.
	 */
	void ScheduleDeferredEditorAutoApply();

	/**
	 * 예약된 Apply And Save 자동 적용을 한 번 실행한다.
	 *
	 * @param deltaTime Ticker가 전달하는 프레임 간격
	 * @return 한 번만 실행하므로 false를 반환한다.
	 */
	bool HandleDeferredEditorAutoApply(float deltaTime);

	/**
	 * Registry의 Binding이 에디터에서 변경됐을 때 호출된다.
	 *
	 * Target만 지정하고 JSON Relative Path를 비워둔 경우
	 * 다음 Tick에서 자동 경로/초기 JSON/Manifest를 갱신한다.
	 */
	void HandleRegistryPropertyChanged(
		UObject* changedObject,
		struct FPropertyChangedEvent& propertyChangedEvent
	);

	/** Registry 변경 처리를 다음 Tick으로 한 번만 예약한다. */
	void ScheduleDeferredManifestRefresh();

	/**
	 * 예약된 Registry 자동 경로/Manifest 갱신을 실행한다.
	 *
	 * @return 한 번 실행 후 해제하므로 false를 반환한다.
	 */
	bool HandleDeferredManifestRefresh(float deltaTime);

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
	 * Registry와 Unreal Reflection을 기준으로 외부 편집기용 Manifest를 갱신한다.
	 *
	 * @param forceRewrite true면 기존 내용과 같아도 다시 저장한다.
	 * @param showSuccessNotification true면 성공 시 에디터 알림을 표시한다.
	 */
	void RefreshExternalDataManifest(
		bool forceRewrite,
		bool showSuccessNotification
	);

	/**
	 * Tools → Rebuild External Data Manifest를 눌렀을 때 강제로 다시 생성한다.
	 */
	void ExecuteRebuildExternalDataManifest();

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
	 * Apply And Save 시작 자동 적용을 다음 Tick으로 넘기는
	 * Core Ticker Delegate Handle이다.
	 */
	FTSTicker::FDelegateHandle deferredEditorAutoApplyHandle;

	/**
	 * Registry Property 변경 감시 Delegate Handle이다.
	 */
	FDelegateHandle registryPropertyChangedHandle;

	/**
	 * Registry 변경 후 다음 Tick에 Manifest/JSON 갱신을 실행하는 Ticker Handle이다.
	 */
	FTSTicker::FDelegateHandle deferredManifestRefreshHandle;

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
