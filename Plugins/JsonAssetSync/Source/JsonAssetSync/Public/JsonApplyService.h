// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JsonApplyTypes.h"

class UJsonApplyRegistry;
class UJsonAssetSyncSettings;

/**
 * Registry에 등록된 외부 JSON을 검사하고 적용하는 공통 서비스다.
 *
 * 에디터와 패키징 게임에서 동일한 처리 로직을 사용한다.
 */
class JSONASSETSYNC_API FJsonApplyService final
{
public:
	/**
	 * Registry 전체를 검사한다.
	 *
	 * 실제 DataTable 또는 DataAsset의 메모리 값은 변경하지 않는다.
	 */
	static FJsonApplySummary ValidateAll(
		const UJsonApplyRegistry* registry,
		const UJsonAssetSyncSettings* settings
	);

	/**
	 * Registry 전체를 검사한 뒤 지원되는 대상에 적용한다.
	 *
	 * DataTable과 DataAsset 모두 실제 메모리 적용을 지원한다.
	 * 에디터에서 Apply And Save 모드이며 allowAssetSave가 true라면
	 * 적용 성공 후 .uasset도 저장한다.
	 *
	 * 패키징 사전검사의 임시 복제본처럼 저장하면 안 되는 대상은
	 * allowAssetSave를 false로 전달한다.
	 */
	static FJsonApplySummary ApplyAll(
		const UJsonApplyRegistry* registry,
		const UJsonAssetSyncSettings* settings,
		const bool allowAssetSave = true
	);

	/**
	 * 전체 검사 또는 적용 결과를 Output Log에 기록한다.
	 */
	static void WriteSummaryToLog(
		const FJsonApplySummary& summary,
		bool logSuccessfulApplications
	);

private:
	/**
	 * 정적 함수만 제공하므로 인스턴스 생성을 막는다.
	 */
	FJsonApplyService() = delete;
};