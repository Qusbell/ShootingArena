// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JsonApplyTypes.h"

class FJsonObject;
class UDataAsset;

/**
 * DataAsset JSON의 리플렉션 검사와 원자적 적용을 담당하는 내부 처리기다.
 *
 * 처리 순서:
 *
 * 1. 대상 DataAsset의 편집 가능한 UPROPERTY 목록 수집
 * 2. 대상 DataAsset을 메모리 전용 임시 객체로 복제
 * 3. JSON 필드와 UPROPERTY 구조 검사
 * 4. 임시 객체에 JSON 전체 타입 변환
 * 5. 모든 검사가 성공한 경우에만 실제 DataAsset에 값 복사
 *
 * JSON 변환에 실패하면 실제 대상 DataAsset은 변경하지 않는다.
 */
class FJsonDataAssetProcessor final
{
public:
	/**
	 * DataAsset JSON을 임시 객체에서 검사한 뒤 실제 대상에 적용한다.
	 *
	 * @param targetDataAsset 실제 적용 대상 DataAsset
	 * @param jsonObject 문법 분석이 완료된 최상위 JSON Object
	 * @param strictValidation 엄격한 필드 검사를 사용할지 여부
	 * @param inOutResult 처리 결과와 상세 오류 목록
	 * @return 실제 DataAsset 적용까지 성공하면 true
	 */
	static bool ValidateAndApply(
		UDataAsset* targetDataAsset,
		const TSharedPtr<FJsonObject>& jsonObject,
		bool strictValidation,
		FJsonApplyResult& inOutResult
	);

private:
	/**
	 * 정적 함수만 제공하므로 외부 인스턴스 생성을 막는다.
	 */
	FJsonDataAssetProcessor() = delete;
};
