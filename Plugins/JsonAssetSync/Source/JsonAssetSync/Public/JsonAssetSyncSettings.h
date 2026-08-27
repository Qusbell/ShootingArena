// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "JsonApplyRegistry.h"
#include "JsonApplyTypes.h"
#include "JsonAssetSyncSettings.generated.h"

/**
 * JSON Asset Sync 시스템의 프로젝트 전역 설정이다.
 *
 * 다음 위치에서 설정할 수 있다.
 *
 * Edit
 * → Project Settings
 * → Plugins
 * → JSON Asset Sync
 */
UCLASS(
	Config = Game,
	DefaultConfig,
	meta = (DisplayName = "JSON Asset Sync")
)
class JSONASSETSYNC_API UJsonAssetSyncSettings :
	public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * JSON Asset Sync 기본 설정값을 지정한다.
	 */
	UJsonAssetSyncSettings();

	/**
	 * Project Settings의 Plugins 카테고리 아래에
	 * 이 설정을 표시한다.
	 */
	virtual FName GetCategoryName() const override;

#if WITH_EDITOR
	/**
	 * Project Settings 좌측에 표시할 섹션 이름이다.
	 */
	virtual FText GetSectionText() const override;

	/**
	 * Project Settings에 표시할 설명이다.
	 */
	virtual FText GetSectionDescription() const override;
#endif

public:
	/**
	 * JSON 파일과 대상 DataTable 또는 DataAsset의 연결을
	 * 관리하는 Registry 에셋이다.
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Registry",
		meta = (
			DisplayName = "Registry Asset",
			ToolTip = "JSON 파일과 대상 에셋의 연결을 관리하는 Registry 에셋입니다."
		)
	)
	TSoftObjectPtr<UJsonApplyRegistry> registryAsset;

	/**
	 * 프로젝트 Content 폴더를 기준으로 하는
	 * DataTable JSON 루트 상대 경로다.
	 *
	 * 기본값:
	 * DataTables
	 *
	 * 실제 경로:
	 * <Project>/Content/DataTables
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Directories",
		meta = (
			DisplayName = "Data Table JSON Directory",
			ToolTip = "Content 폴더를 기준으로 하는 DataTable JSON 루트 상대 경로입니다."
		)
	)
	FString dataTableJsonDirectory;


	/**
	 * 프로젝트 Content 폴더를 기준으로 하는
	 * CurveTable JSON 루트 상대 경로다.
	 *
	 * 기본값:
	 * CurveTables
	 *
	 * 실제 경로:
	 * <Project>/Content/CurveTables
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Directories",
		meta = (
			DisplayName = "Curve Table JSON Directory",
			ToolTip = "Content 폴더를 기준으로 하는 CurveTable JSON 루트 상대 경로입니다."
		)
	)
	FString curveTableJsonDirectory;

	/**
	 * 프로젝트 Content 폴더를 기준으로 하는
	 * Curve Float JSON 루트 상대 경로다.
	 *
	 * 기본값:
	 * FloatCurves
	 *
	 * 실제 경로:
	 * <Project>/Content/FloatCurves
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Directories",
		meta = (
			DisplayName = "Float Curve JSON Directory",
			ToolTip = "Content 폴더를 기준으로 하는 Curve Float JSON 루트 상대 경로입니다."
		)
	)
	FString floatCurveJsonDirectory;

	/**
	 * 프로젝트 Content 폴더를 기준으로 하는
	 * DataAsset JSON 루트 상대 경로다.
	 *
	 * 기본값:
	 * DataAssets
	 *
	 * 실제 경로:
	 * <Project>/Content/DataAssets
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Directories",
		meta = (
			DisplayName = "Data Asset JSON Directory",
			ToolTip = "Content 폴더를 기준으로 하는 DataAsset JSON 루트 상대 경로입니다."
		)
	)
	FString dataAssetJsonDirectory;

	/**
	 * Unreal Editor 시작 시 Registry의 JSON을
	 * 자동으로 검사하고 적용할지를 결정한다.
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Automatic Application",
		meta = (
			DisplayName = "Apply On Editor Startup",
			ToolTip = "Unreal Editor 시작 시 Registry의 JSON을 자동으로 적용합니다."
		)
	)
	bool applyOnEditorStartup;

	/**
	 * 패키징된 게임 시작 시 Registry의 JSON을
	 * 자동으로 검사하고 적용할지를 결정한다.
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Automatic Application",
		meta = (
			DisplayName = "Apply On Runtime Startup",
			ToolTip = "패키징된 게임 시작 시 Registry의 JSON을 자동으로 적용합니다."
		)
	)
	bool applyOnRuntimeStartup;

	/**
	 * 에디터에서 JSON 적용 결과를 처리하는 방식이다.
	 *
	 * 패키징 게임에서는 항상 실행 중인 메모리에만 적용된다.
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Automatic Application",
		meta = (
			DisplayName = "Editor Apply Mode",
			ToolTip = "에디터에서 메모리에만 적용할지 실제 에셋까지 저장할지 결정합니다."
		)
	)
	EJsonEditorApplyMode editorApplyMode;

	/**
	 * 알 수 없는 필드, 누락된 필드, 타입 불일치 등을
	 * 엄격하게 검사할지를 결정한다.
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Validation",
		meta = (
			DisplayName = "Strict Validation",
			ToolTip = "JSON 필드와 대상 Unreal 프로퍼티를 엄격하게 검사합니다."
		)
	)
	bool strictValidation;

	/**
	 * 정상적으로 적용된 항목도 Output Log에 기록할지를 결정한다.
	 *
	 * 실패와 경고는 이 옵션과 관계없이 출력한다.
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Logging",
		meta = (
			DisplayName = "Log Successful Applications",
			ToolTip = "정상적으로 적용된 JSON 항목도 Output Log에 출력합니다."
		)
	)
	bool logSuccessfulApplications;

	/**
	 * JSON 검사 또는 적용이 완료됐을 때
	 * 에디터 오른쪽 아래에 알림을 표시할지를 결정한다.
	 *
	 * 패키징 게임에서는 사용되지 않는다.
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Editor Feedback",
		meta = (
			DisplayName = "Show Editor Notifications",
			ToolTip = "JSON 처리 완료 시 에디터 오른쪽 아래에 성공 또는 실패 알림을 표시합니다."
		)
	)
	bool showEditorNotifications;

	/**
	 * JSON 적용에 실패한 항목이 있을 때
	 * 전용 Message Log를 자동으로 열지를 결정한다.
	 *
	 * 패키징 게임에서는 사용되지 않는다.
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Editor Feedback",
		meta = (
			DisplayName = "Open Message Log On Failure",
			ToolTip = "JSON 처리 실패 시 JSON Asset Sync Message Log를 자동으로 엽니다."
		)
	)
	bool openMessageLogOnFailure;

	/**
	 * 에디터 시작 시 패키징 준비 상태를 자동으로 검사할지를 결정한다.
	 *
	 * 실제 패키징을 실행하지 않고도 외부 JSON 복사 설정,
	 * Registry Cook 설정, Binding, JSON 타입 변환을 검사한다.
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Packaging Safety",
		meta = (
			DisplayName = "Run Packaging Preflight On Editor Startup",
			ToolTip = "에디터 시작 시 JSON Asset Sync의 패키징 준비 상태를 자동 검사합니다."
		)
	)
	bool runPackagingPreflightOnEditorStartup;

	/**
	 * 패키징에 필요한 설정이 빠져 있을 때 자동으로 추가할지를 결정한다.
	 *
	 * 활성화하면 다음 항목을 DefaultGame.ini에 자동 반영한다.
	 *
	 * - DataTables/DataAssets 외부 폴더 복사 설정
	 * - Registry 폴더 Always Cook 설정
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		Category = "Packaging Safety",
		meta = (
			DisplayName = "Auto Configure Packaging Settings",
			ToolTip = "누락된 외부 JSON 복사 경로와 Registry Cook 경로를 패키징 설정에 자동 추가합니다."
		)
	)
	bool autoConfigurePackagingSettings;
};
