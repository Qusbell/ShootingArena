// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JsonApplyTypes.h"

class UCurveFloat;

/**
 * 외부 JSON을 UCurveFloat의 FRichCurve 데이터로 검사하고 적용한다.
 *
 * JSON은 FRichCurve의 Reflection 직렬화 형식을 사용하므로
 * Time/Value뿐 아니라 InterpMode, TangentMode, Tangent,
 * TangentWeight, Pre/Post Infinity Extrapolation도 함께 보존한다.
 */
class FJsonFloatCurveProcessor
{
public:
	/**
	 * JSON 문자열을 임시 FRichCurve에 먼저 변환한 뒤
	 * 모든 검사가 성공한 경우에만 Target Curve Float에 Commit한다.
	 */
	static bool ValidateAndApply(
		UCurveFloat* targetFloatCurve,
		const FString& jsonText,
		bool strictValidation,
		FJsonApplyResult& inOutResult
	);
};
