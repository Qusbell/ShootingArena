// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonCurveTableProcessor.h"

#include "Engine/CurveTable.h"
#include "Curves/RichCurve.h"
#include "Curves/SimpleCurve.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace JsonAssetSync::CurveTablePrivate
{
	/** CurveTable 처리 중 발생한 문제를 결과에 추가한다. */
	void AddIssue(
		FJsonApplyResult& inOutResult,
		const EJsonApplyIssueStage stage,
		const EJsonApplyIssueSeverity severity,
		const FString& message
	)
	{
		FJsonApplyIssue issue;
		issue.stage = stage;
		issue.severity = severity;
		issue.sourceJsonPath = inOutResult.sourceJsonPath;
		issue.targetAssetPath = inOutResult.targetAssetPath;
		issue.message = message;
		inOutResult.issues.Add(MoveTemp(issue));
	}

	/** UCurveTable API가 반환한 문제 문자열들을 공통 결과 형식으로 변환한다. */
	void AddProblemMessages(
		FJsonApplyResult& inOutResult,
		const TArray<FString>& problems,
		const EJsonApplyIssueStage stage,
		const FString& prefix
	)
	{
		for (const FString& problem : problems)
		{
			AddIssue(
				inOutResult,
				stage,
				EJsonApplyIssueSeverity::Error,
				FString::Printf(
					TEXT("%s%s"),
					*prefix,
					*problem
				)
			);
		}
	}

	/**
	 * 대상 CurveTable에서 JSON Import에 사용할 보간 모드를 결정한다.
	 *
	 * - SimpleCurve: 첫 번째 Curve가 가진 공통 보간 모드를 사용한다.
	 * - RichCurve: 첫 번째 Curve의 첫 번째 Key 보간 모드를 사용한다.
	 * - 비어 있거나 Key가 없으면 Linear를 안전한 기본값으로 사용한다.
	 *
	 * UCurveTable에는 ImportCurveInterpMode라는 공개 멤버가 없으므로
	 * UE 5.6의 공개 Curve API만 사용해서 판단한다.
	 */
	ERichCurveInterpMode ResolveInterpMode(
		const UCurveTable* curveTable
	)
	{
		if (!IsValid(curveTable))
		{
			return ERichCurveInterpMode::RCIM_Linear;
		}

		switch (curveTable->GetCurveTableMode())
		{
		case ECurveTableMode::SimpleCurves:
		{
			const TMap<FName, FSimpleCurve*>& rowMap =
				curveTable->GetSimpleCurveRowMap();

			for (const TPair<FName, FSimpleCurve*>& pair : rowMap)
			{
				if (pair.Value != nullptr)
				{
					return pair.Value->GetKeyInterpMode();
				}
			}

			break;
		}

		case ECurveTableMode::RichCurves:
		{
			const TMap<FName, FRichCurve*>& rowMap =
				curveTable->GetRichCurveRowMap();

			for (const TPair<FName, FRichCurve*>& pair : rowMap)
			{
				if (pair.Value == nullptr)
				{
					continue;
				}

				const TArray<FRichCurveKey>& keys =
					pair.Value->GetConstRefOfKeys();

				if (keys.Num() > 0)
				{
					return keys[0].InterpMode.GetValue();
				}
			}

			break;
		}

		case ECurveTableMode::Empty:
		default:
			break;
		}

		return ERichCurveInterpMode::RCIM_Linear;
	}

	/** 저장되지 않는 메모리 전용 CurveTable을 생성한다. */
	UCurveTable* CreateTransientCurveTable()
	{
		return NewObject<UCurveTable>(
			GetTransientPackage(),
			NAME_None,
			RF_Transient
		);
	}
}

bool FJsonCurveTableProcessor::ValidateAndApply(
	UCurveTable* targetCurveTable,
	const FString& jsonText,
	const bool strictValidation,
	FJsonApplyResult& inOutResult
)
{
	using namespace JsonAssetSync::CurveTablePrivate;

	inOutResult.wasApplied = false;

	/*
	 * CurveTable의 JSON Import API 자체가 잘못된 Row/Key 데이터를
	 * 문제 문자열로 반환하므로 Strict Validation과 관계없이
	 * 실제 변환 오류는 항상 실패시킨다.
	 *
	 * 현재 CurveTable JSON 규격에는 임의 UPROPERTY가 없어서
	 * DataTable/DataAsset처럼 Extra/Missing Field 정책을 따로 둘 필요가 없다.
	 */
	static_cast<void>(strictValidation);

	if (!IsValid(targetCurveTable))
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT("적용할 CurveTable이 유효하지 않습니다.")
		);
		return false;
	}

	/*
	 * 먼저 대상과 동일한 CurveTable을 임시 객체에 복사한다.
	 * 이를 통해 기존 CurveTableMode를 유지한 상태에서
	 * JSON 변환을 시험할 수 있다.
	 */
	UCurveTable* stagedCurveTable = CreateTransientCurveTable();
	if (!IsValid(stagedCurveTable))
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT("검사용 임시 CurveTable을 생성하지 못했습니다.")
		);
		return false;
	}

	const TArray<FString> stagedSetupProblems =
		stagedCurveTable->CreateTableFromOtherTable(targetCurveTable);

	if (stagedSetupProblems.Num() > 0)
	{
		AddProblemMessages(
			inOutResult,
			stagedSetupProblems,
			EJsonApplyIssueStage::Conversion,
			TEXT("검사용 CurveTable 준비 실패: ")
		);
		return false;
	}

	stagedCurveTable->EmptyTable();

	/*
	 * Unreal의 CurveTable JSON 형식은
	 * [{ "Name":"Row", "0":1.0, "1":2.0 }] 형태이며,
	 * CreateTableFromJSONString이 Time/Value Key를 실제 Curve로 변환한다.
	 */
	/*
	 * UE 5.6의 UCurveTable에는 ImportCurveInterpMode 멤버가 없다.
	 * 기존 Curve의 보간 모드를 공개 API로 확인하고,
	 * 비어 있는 CurveTable이면 Linear를 기본값으로 사용한다.
	 */
	const ERichCurveInterpMode interpMode =
		ResolveInterpMode(targetCurveTable);

	const TArray<FString> importProblems =
		stagedCurveTable->CreateTableFromJSONString(
			jsonText,
			interpMode
		);

	if (importProblems.Num() > 0)
	{
		AddProblemMessages(
			inOutResult,
			importProblems,
			EJsonApplyIssueStage::Conversion,
			TEXT("CurveTable JSON 변환 실패: ")
		);
		return false;
	}

	/*
	 * Commit 실패 시 즉시 복구할 수 있도록 원본을 백업한다.
	 */
	UCurveTable* backupCurveTable = CreateTransientCurveTable();
	if (!IsValid(backupCurveTable))
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Commit,
			EJsonApplyIssueSeverity::Error,
			TEXT("원본 CurveTable 백업 객체를 생성하지 못했습니다.")
		);
		return false;
	}

	const TArray<FString> backupProblems =
		backupCurveTable->CreateTableFromOtherTable(targetCurveTable);

	if (backupProblems.Num() > 0)
	{
		AddProblemMessages(
			inOutResult,
			backupProblems,
			EJsonApplyIssueStage::Commit,
			TEXT("원본 CurveTable 백업 실패: ")
		);
		return false;
	}

	const TArray<FString> commitProblems =
		targetCurveTable->CreateTableFromOtherTable(stagedCurveTable);

	if (commitProblems.Num() > 0)
	{
		AddProblemMessages(
			inOutResult,
			commitProblems,
			EJsonApplyIssueStage::Commit,
			TEXT("CurveTable 적용 실패: ")
		);

		const TArray<FString> rollbackProblems =
			targetCurveTable->CreateTableFromOtherTable(backupCurveTable);

		if (rollbackProblems.Num() > 0)
		{
			AddProblemMessages(
				inOutResult,
				rollbackProblems,
				EJsonApplyIssueStage::Commit,
				TEXT("CurveTable 원본 복구 실패: ")
			);
		}

		targetCurveTable->OnCurveTableChanged().Broadcast();
		UCurveTable::InvalidateAllCachedCurves();
		return false;
	}

	/*
	 * CurveTable을 참조하는 FCurveTableRowHandle/FScalableFloat 등이
	 * 변경 사실을 즉시 인지하도록 변경 Delegate와 전역 Cache를 갱신한다.
	 */
	targetCurveTable->OnCurveTableChanged().Broadcast();
	UCurveTable::InvalidateAllCachedCurves();

	inOutResult.wasApplied = true;
	return true;
}
