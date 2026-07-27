// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JsonApplyTypes.h"

class UDataTable;

/**
 * DataTable JSON의 변환과 원자적 적용을 담당하는 내부 처리기다.
 *
 * JSON을 실제 DataTable에 바로 적용하지 않고,
 * 임시 DataTable에서 먼저 전체 변환을 완료한다.
 *
 * 변환이 모두 성공한 경우에만 실제 대상 DataTable을 교체한다.
 */
class FJsonDataTableProcessor final
{
public:
	/**
	 * JSON 문자열을 임시 DataTable에 변환한 뒤
	 * 실제 대상 DataTable에 적용한다.
	 *
	 * @param targetDataTable 실제로 변경할 대상 DataTable
	 * @param jsonText 파일에서 읽은 전체 JSON 문자열
	 * @param strictValidation 엄격한 필드 검사를 사용할지 여부
	 * @param inOutResult 처리 결과와 오류 목록
	 * @return 실제 DataTable 적용까지 성공하면 true
	 */
	static bool ValidateAndApply(
		UDataTable* targetDataTable,
		const FString& jsonText,
		bool strictValidation,
		FJsonApplyResult& inOutResult
	);

private:
	/**
	 * 정적 함수만 사용하는 처리기이므로
	 * 외부에서 인스턴스를 만들 수 없도록 한다.
	 */
	FJsonDataTableProcessor() = delete;
};