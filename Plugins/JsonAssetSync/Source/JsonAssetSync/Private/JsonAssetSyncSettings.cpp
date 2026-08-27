// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonAssetSyncSettings.h"

UJsonAssetSyncSettings::UJsonAssetSyncSettings()
{
	/*
	 * Content 폴더를 기준으로 사용하는
	 * JSON 루트 상대 경로 기본값이다.
	 */
	dataTableJsonDirectory = TEXT("DataTables");
	curveTableJsonDirectory = TEXT("CurveTables");
	floatCurveJsonDirectory = TEXT("FloatCurves");
	dataAssetJsonDirectory = TEXT("DataAssets");

	/*
	 * 에디터와 패키징 게임 모두 시작 시
	 * JSON을 자동으로 적용하도록 기본 설정한다.
	 */
	applyOnEditorStartup = true;
	applyOnRuntimeStartup = true;

	/*
	 * JSON을 지속적인 데이터 원본으로 사용하므로
	 * 기본적으로 .uasset은 저장하지 않고 메모리에만 적용한다.
	 */
	editorApplyMode =
		EJsonEditorApplyMode::MemoryOnly;

	/*
	 * 잘못된 필드나 타입이 일부만 적용되는 상황을 막기 위해
	 * 엄격 검사를 기본으로 활성화한다.
	 */
	strictValidation = true;

	/*
	 * 개발 초기에는 성공한 항목도 확인할 수 있도록
	 * 성공 로그 출력을 기본으로 활성화한다.
	 */
	logSuccessfulApplications = true;

	/*
	 * Output Log를 직접 열지 않아도 결과를 알 수 있도록
	 * 에디터 알림을 기본으로 활성화한다.
	 */
	showEditorNotifications = true;

	/*
	 * 실패한 항목이 있을 때 상세 오류를 바로 볼 수 있도록
	 * Message Log 자동 열기를 기본으로 활성화한다.
	 */
	openMessageLogOnFailure = true;

	/*
	 * 실제 패키징을 자주 실행하기 어려운 상황에서도
	 * 설정 누락과 JSON 타입 오류를 미리 확인하도록 활성화한다.
	 */
	runPackagingPreflightOnEditorStartup = true;

	/*
	 * 외부 JSON 복사 경로와 Registry Cook 경로 누락으로 인한
	 * 패키징 실패 가능성을 줄이기 위해 자동 설정을 활성화한다.
	 */
	autoConfigurePackagingSettings = true;
}

FName UJsonAssetSyncSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR

FText UJsonAssetSyncSettings::GetSectionText() const
{
	return NSLOCTEXT(
		"JsonAssetSyncSettings",
		"SectionText",
		"JSON Asset Sync"
	);
}

FText UJsonAssetSyncSettings::GetSectionDescription() const
{
	return NSLOCTEXT(
		"JsonAssetSyncSettings",
		"SectionDescription",
		"외부 JSON 파일을 Registry에 등록된 DataTable, CurveTable, FloatCurve, DataAsset에 검사하고 적용합니다."
	);
}

#endif
