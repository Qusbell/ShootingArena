// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JsonApplyTypes.h"

class UCurveTable;

/**
 * CurveTable JSON의 변환과 원자적 적용을 담당하는 내부 처리기다.
 *
 * Unreal의 UCurveTable::CreateTableFromJSONString을 사용하여
 * 임시 CurveTable에서 전체 변환을 검증한 뒤 실제 대상에 Commit한다.
 */
class FJsonCurveTableProcessor final
{
public:
	/**
	 * JSON 문자열을 임시 CurveTable에 변환한 뒤 실제 대상에 적용한다.
	 *
	 * 대상 CurveTable의 ImportCurveInterpMode를 그대로 사용하므로
	 * 외부 JSON은 Row/Key의 Time/Value 데이터를 소유하고,
	 * 보간 방식은 대상 CurveTable 설정을 따른다.
	 */
	static bool ValidateAndApply(
		UCurveTable* targetCurveTable,
		const FString& jsonText,
		bool strictValidation,
		FJsonApplyResult& inOutResult
	);

private:
	FJsonCurveTableProcessor() = delete;
};
