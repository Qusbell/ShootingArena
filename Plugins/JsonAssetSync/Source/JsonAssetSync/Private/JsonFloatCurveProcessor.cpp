// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonFloatCurveProcessor.h"

#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "JsonObjectConverter.h"

namespace JsonAssetSync::FloatCurvePrivate
{
	/** Curve Float 처리 중 발생한 문제를 공통 결과에 추가한다. */
	void AddIssue(
		FJsonApplyResult& inOutResult,
		const EJsonApplyIssueStage stage,
		const EJsonApplyIssueSeverity severity,
		const FString& message,
		const FString& propertyPath = FString()
	)
	{
		FJsonApplyIssue issue;
		issue.stage = stage;
		issue.severity = severity;
		issue.sourceJsonPath = inOutResult.sourceJsonPath;
		issue.targetAssetPath = inOutResult.targetAssetPath;
		issue.propertyPath = propertyPath;
		issue.message = message;
		inOutResult.issues.Add(MoveTemp(issue));
	}

	/**
	 * Key 배열의 Time이 오름차순이 되도록 정렬하고,
	 * 동일 Time Key가 중복되어 있지 않은지 검사한다.
	 */
	bool NormalizeAndValidateKeys(
		FRichCurve& curve,
		FJsonApplyResult& inOutResult
	)
	{
		curve.Keys.Sort(
			[](const FRichCurveKey& left, const FRichCurveKey& right)
			{
				return left.Time < right.Time;
			}
		);

		for (int32 index = 1; index < curve.Keys.Num(); ++index)
		{
			const float previousTime = curve.Keys[index - 1].Time;
			const float currentTime = curve.Keys[index].Time;

			if (FMath::IsNearlyEqual(
				previousTime,
				currentTime,
				KINDA_SMALL_NUMBER
			))
			{
				AddIssue(
					inOutResult,
					EJsonApplyIssueStage::Structure,
					EJsonApplyIssueSeverity::Error,
					FString::Printf(
						TEXT(
							"Curve Float에 동일한 Time의 Key가 "
							"중복되어 있습니다: %s"
						),
						*FString::SanitizeFloat(currentTime)
					),
					FString::Printf(
						TEXT("keys[%d].time"),
						index
					)
				);

				return false;
			}
		}

		return true;
	}
}

bool FJsonFloatCurveProcessor::ValidateAndApply(
	UCurveFloat* targetFloatCurve,
	const FString& jsonText,
	const bool strictValidation,
	FJsonApplyResult& inOutResult
)
{
	using namespace JsonAssetSync::FloatCurvePrivate;

	inOutResult.wasApplied = false;

	if (!IsValid(targetFloatCurve))
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT("적용할 Curve Float 에셋이 유효하지 않습니다.")
		);
		return false;
	}

	/*
	 * 원본 에셋은 검사가 끝날 때까지 수정하지 않는다.
	 * 현재 값으로 초기화하면 Strict Validation을 끈 경우
	 * JSON에 없는 필드는 기존 값을 유지할 수 있다.
	 */
	FRichCurve stagedCurve = targetFloatCurve->FloatCurve;

	FText failReason;
	const bool converted =
		FJsonObjectConverter::JsonObjectStringToUStruct(
			jsonText,
			&stagedCurve,
			0,
			0,
			strictValidation,
			&failReason,
			nullptr
		);

	if (!converted)
	{
		const FString reason =
			failReason.IsEmpty()
				? TEXT("알 수 없는 FRichCurve 변환 오류")
				: failReason.ToString();

		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			FString::Printf(
				TEXT("Curve Float JSON 변환에 실패했습니다: %s"),
				*reason
			)
		);

		return false;
	}

	if (!NormalizeAndValidateKeys(
		stagedCurve,
		inOutResult
	))
	{
		return false;
	}

	/*
	 * Auto Tangent Key는 현재 Key 위치를 기준으로 다시 계산한다.
	 * User/Break Tangent는 JSON에 저장된 값을 그대로 유지한다.
	 */
	stagedCurve.AutoSetTangents(0.0f);

#if WITH_EDITOR
	targetFloatCurve->Modify();
#endif

	targetFloatCurve->FloatCurve = MoveTemp(stagedCurve);

#if WITH_EDITOR
	/*
	 * Curve Editor, Details Panel 등 에디터 UI가 변경된 Curve를
	 * 즉시 다시 그릴 수 있도록 변경 통지를 보낸다.
	 */
	targetFloatCurve->PostEditChange();
#endif

	inOutResult.wasApplied = true;
	return true;
}
