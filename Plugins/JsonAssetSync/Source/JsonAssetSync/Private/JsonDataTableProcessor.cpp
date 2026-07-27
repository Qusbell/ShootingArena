// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonDataTableProcessor.h"

#include "Engine/DataTable.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace JsonAssetSync::DataTablePrivate
{
	/**
	 * DataTable 처리 중 발생한 문제를 결과에 추가한다.
	 */
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

	/**
	 * UDataTable 함수가 반환한 문제 문자열들을
	 * FJsonApplyIssue 배열로 변환한다.
	 */
	void AddProblemMessages(
		FJsonApplyResult& inOutResult,
		const TArray<FString>& problems,
		const EJsonApplyIssueStage stage,
		const FString& messagePrefix
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
					*messagePrefix,
					*problem
				)
			);
		}
	}

	/**
	 * 프로젝트 에셋으로 저장되지 않는
	 * 메모리 전용 임시 DataTable을 생성한다.
	 */
	UDataTable* CreateTransientDataTable()
	{
		return NewObject<UDataTable>(
			GetTransientPackage(),
			NAME_None,
			RF_Transient
		);
	}

	/**
	 * Commit 과정에서 변경될 가능성이 있는
	 * DataTable Import Option을 복구한다.
	 */
	void RestoreImportOptions(
		UDataTable* targetDataTable,
		const bool originalIgnoreExtraFields,
		const bool originalIgnoreMissingFields,
		const bool originalPreserveExistingValues,
		const FString& originalImportKeyField
	)
	{
		if (!IsValid(targetDataTable))
		{
			return;
		}

		targetDataTable->bIgnoreExtraFields =
			originalIgnoreExtraFields;

		targetDataTable->bIgnoreMissingFields =
			originalIgnoreMissingFields;

		targetDataTable->bPreserveExistingValues =
			originalPreserveExistingValues;

		targetDataTable->ImportKeyField =
			originalImportKeyField;
	}
}

bool FJsonDataTableProcessor::ValidateAndApply(
	UDataTable* targetDataTable,
	const FString& jsonText,
	const bool strictValidation,
	FJsonApplyResult& inOutResult
)
{
	using namespace JsonAssetSync::DataTablePrivate;

	inOutResult.wasApplied = false;

	/*
	 * Registry 단계에서도 검사하지만,
	 * 실제 적용 직전에 대상 유효성을 한 번 더 확인한다.
	 */
	if (!IsValid(targetDataTable))
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT("적용할 DataTable이 유효하지 않습니다.")
		);

		return false;
	}

	/*
	 * JSON을 DataTable Row로 변환하려면
	 * 대상 DataTable에 RowStruct가 지정되어 있어야 한다.
	 */
	if (targetDataTable->GetRowStruct() == nullptr)
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT("대상 DataTable에 RowStruct가 지정되어 있지 않습니다.")
		);

		return false;
	}

	/*
	 * 실제 대상 DataTable의 Import Option을 기억한다.
	 *
	 * 임시 테이블의 Strict Validation 설정이
	 * 실제 대상 에셋의 설정까지 바꾸지 않게 하기 위함이다.
	 */
	const bool originalIgnoreExtraFields =
		targetDataTable->bIgnoreExtraFields;

	const bool originalIgnoreMissingFields =
		targetDataTable->bIgnoreMissingFields;

	const bool originalPreserveExistingValues =
		targetDataTable->bPreserveExistingValues;

	const FString originalImportKeyField =
		targetDataTable->ImportKeyField;

	/*
	 * JSON 변환을 먼저 시험할 임시 DataTable을 생성한다.
	 */
	UDataTable* stagedDataTable =
		CreateTransientDataTable();

	if (!IsValid(stagedDataTable))
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT("검사용 임시 DataTable을 생성하지 못했습니다.")
		);

		return false;
	}

	/*
	 * 대상 DataTable을 임시 DataTable에 복사한다.
	 *
	 * 이를 통해 RowStruct와 Import 관련 설정을
	 * 대상과 동일한 상태로 만든다.
	 */
	const TArray<FString> stagedSetupProblems =
		stagedDataTable->CreateTableFromOtherTable(
			targetDataTable
		);

	if (stagedSetupProblems.Num() > 0)
	{
		AddProblemMessages(
			inOutResult,
			stagedSetupProblems,
			EJsonApplyIssueStage::Conversion,
			TEXT("검사용 DataTable 준비 실패: ")
		);

		return false;
	}

	/*
	 * 기존 Row만 제거한다.
	 *
	 * EmptyTable은 RowStruct를 제거하지 않으므로
	 * 대상과 동일한 RowStruct를 유지한 상태에서
	 * 새 JSON을 변환할 수 있다.
	 */
	stagedDataTable->EmptyTable();

	/*
	 * Strict Validation이 활성화되면
	 * JSON에 불필요한 필드가 있거나 필요한 필드가 빠진 경우
	 * UDataTable Importer가 문제로 보고하도록 한다.
	 *
	 * 비활성화하면 Extra Field와 Missing Field를 허용하지만,
	 * 타입 불일치 등 실제 변환 문제는 여전히 실패한다.
	 */
	stagedDataTable->bIgnoreExtraFields =
		!strictValidation;

	stagedDataTable->bIgnoreMissingFields =
		!strictValidation;

	/*
	 * JSON에 없는 필드가 이전 Row 값을 유지하지 않도록 한다.
	 *
	 * 새 JSON은 현재 실행에서 사용할 전체 데이터 원본이므로
	 * 임시 테이블의 기본값을 기준으로 변환한다.
	 */
	stagedDataTable->bPreserveExistingValues = false;

	/*
	 * JSON 전체를 임시 DataTable에 변환한다.
	 *
	 * 실제 대상 DataTable은 아직 변경되지 않는다.
	 */
	const TArray<FString> importProblems =
		stagedDataTable->CreateTableFromJSONString(
			jsonText
		);

	/*
	 * Importer가 문제를 하나라도 반환했다면
	 * 실제 대상에는 적용하지 않는다.
	 *
	 * 이 정책으로 특정 Row만 부분 적용되는 상황을 막는다.
	 */
	if (importProblems.Num() > 0)
	{
		AddProblemMessages(
			inOutResult,
			importProblems,
			EJsonApplyIssueStage::Conversion,
			TEXT("DataTable JSON 변환 실패: ")
		);

		return false;
	}

	/*
	 * 변환 이후에도 RowStruct가 대상과 동일한지 확인한다.
	 */
	if (stagedDataTable->GetRowStruct() !=
		targetDataTable->GetRowStruct())
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT(
				"검사용 DataTable의 RowStruct가 "
				"대상 DataTable과 일치하지 않습니다."
			)
		);

		return false;
	}

	/*
	 * Commit이 실패했을 때 원본을 복구하기 위한
	 * 메모리 전용 백업 DataTable을 만든다.
	 */
	UDataTable* backupDataTable =
		CreateTransientDataTable();

	if (!IsValid(backupDataTable))
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Commit,
			EJsonApplyIssueSeverity::Error,
			TEXT("원본 DataTable 백업 객체를 생성하지 못했습니다.")
		);

		return false;
	}

	const TArray<FString> backupProblems =
		backupDataTable->CreateTableFromOtherTable(
			targetDataTable
		);

	if (backupProblems.Num() > 0)
	{
		AddProblemMessages(
			inOutResult,
			backupProblems,
			EJsonApplyIssueStage::Commit,
			TEXT("원본 DataTable 백업 실패: ")
		);

		return false;
	}

	/*
	 * 임시 테이블의 JSON 변환이 완전히 성공한 뒤에만
	 * 실제 대상 DataTable에 복사한다.
	 */
	const TArray<FString> commitProblems =
		targetDataTable->CreateTableFromOtherTable(
			stagedDataTable
		);

	if (commitProblems.Num() > 0)
	{
		AddProblemMessages(
			inOutResult,
			commitProblems,
			EJsonApplyIssueStage::Commit,
			TEXT("DataTable 적용 실패: ")
		);

		/*
		 * Commit 과정에서 대상이 일부 변경됐을 가능성이 있으므로
		 * 적용 전 백업으로 즉시 복구한다.
		 */
		const TArray<FString> rollbackProblems =
			targetDataTable->CreateTableFromOtherTable(
				backupDataTable
			);

		if (rollbackProblems.Num() > 0)
		{
			AddProblemMessages(
				inOutResult,
				rollbackProblems,
				EJsonApplyIssueStage::Commit,
				TEXT("DataTable 원본 복구 실패: ")
			);
		}

		RestoreImportOptions(
			targetDataTable,
			originalIgnoreExtraFields,
			originalIgnoreMissingFields,
			originalPreserveExistingValues,
			originalImportKeyField
		);

		/*
		 * 전체 Row가 복구됐음을 DataTable 사용자에게 알린다.
		 */
		targetDataTable->HandleDataTableChanged(NAME_None);

		return false;
	}

	/*
	 * 임시 테이블 설정이 실제 대상 설정을 바꾸지 않도록
	 * 원래 Import Option을 복구한다.
	 */
	RestoreImportOptions(
		targetDataTable,
		originalIgnoreExtraFields,
		originalIgnoreMissingFields,
		originalPreserveExistingValues,
		originalImportKeyField
	);

	/*
	 * 전체 Row가 변경됐음을 DataTable Delegate와
	 * Row Callback에 알린다.
	 *
	 * NAME_None은 특정 Row 하나가 아니라
	 * 전체 테이블이 변경됐다는 의미로 사용한다.
	 */
	targetDataTable->HandleDataTableChanged(NAME_None);

	inOutResult.wasApplied = true;

	return true;
}