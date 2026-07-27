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
	 * 현재 단계에서는 DataTable 실제 적용을 지원한다.
	 * DataAsset은 구조 검사까지만 수행한다.
	 */
	static FJsonApplySummary ApplyAll(
		const UJsonApplyRegistry* registry,
		const UJsonAssetSyncSettings* settings
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