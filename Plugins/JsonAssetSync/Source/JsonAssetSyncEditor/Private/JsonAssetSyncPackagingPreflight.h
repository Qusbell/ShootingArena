// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JsonApplyTypes.h"

/**
 * JSON Asset Sync 패키징 사전검사 결과다.
 */
struct FJsonAssetSyncPackagingPreflightReport
{
	/** 패키징 준비 상태가 정상인지 여부다. */
	bool isReady = false;

	/** 누락된 패키징 설정을 자동으로 추가했는지 여부다. */
	bool packagingSettingsChanged = false;

	/** 패키징 설정과 경로 검사에서 발생한 문제 목록이다. */
	TArray<FJsonApplyIssue> issues;

	/**
	 * 실제 에셋의 복제본에 JSON을 적용해 본 결과다.
	 *
	 * 원본 DataTable과 DataAsset은 변경하지 않는다.
	 */
	FJsonApplySummary dryRunSummary;
};

/**
 * 실제 패키징을 실행하지 않고 JSON Asset Sync의
 * 패키징 준비 상태를 검사하는 에디터 전용 처리기다.
 */
class FJsonAssetSyncPackagingPreflight final
{
public:
	/**
	 * 패키징 준비 상태를 검사한다.
	 *
	 * @param autoConfigurePackagingSettings true면 누락된 패키징 설정을 자동 추가한다.
	 * @return 패키징 설정과 JSON Dry Run 결과
	 */
	static FJsonAssetSyncPackagingPreflightReport Run(
		bool autoConfigurePackagingSettings
	);

private:
	/**
	 * 정적 함수만 제공하므로 외부 인스턴스 생성을 막는다.
	 */
	FJsonAssetSyncPackagingPreflight() = delete;
};
