// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "JsonApplyRegistry.generated.h"

/**
 * DataTable용 JSON 파일과 대상 DataTable 에셋을 연결한다.
 *
 * jsonRelativePath는 Content/DataTables를 기준으로 작성한다.
 *
 * 예:
 * jsonRelativePath = "Character/CharacterData.json"
 *
 * 실제 파일:
 * <Project>/Content/DataTables/Character/CharacterData.json
 */
USTRUCT(BlueprintType)
struct JSONASSETSYNC_API FJsonDataTableBinding
{
	GENERATED_BODY()

public:
	/**
	 * Content/DataTables 폴더를 기준으로 하는 JSON 상대 경로다.
	 *
	 * Content/DataTables 자체는 입력하지 않는다.
	 *
	 * 올바른 예:
	 * TestData.json
	 * Character/CharacterData.json
	 *
	 * 잘못된 예:
	 * Content/DataTables/TestData.json
	 * C:/Project/Content/DataTables/TestData.json
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "JSON Asset Sync|Binding",
		meta = (
			DisplayName = "JSON Relative Path",
			ToolTip = "Content/DataTables 폴더를 기준으로 하는 JSON 상대 경로입니다."
			)
	)
	FString jsonRelativePath;

	/**
	 * JSON 값을 적용할 실제 DataTable 에셋이다.
	 *
	 * Registry에 이 참조를 등록하는 순간,
	 * 해당 DataTable은 JSON 자동 적용 대상이 된다.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "JSON Asset Sync|Binding",
		meta = (
			DisplayName = "Target Data Table",
			ToolTip = "JSON 값을 적용할 대상 DataTable 에셋입니다."
			)
	)
	TObjectPtr<UDataTable> targetDataTable = nullptr;
};

/**
 * DataAsset용 JSON 파일과 대상 DataAsset 에셋을 연결한다.
 *
 * jsonRelativePath는 Content/DataAssets를 기준으로 작성한다.
 *
 * 예:
 * jsonRelativePath = "Settings/GlobalSettings.json"
 *
 * 실제 파일:
 * <Project>/Content/DataAssets/Settings/GlobalSettings.json
 */
USTRUCT(BlueprintType)
struct JSONASSETSYNC_API FJsonDataAssetBinding
{
	GENERATED_BODY()

public:
	/**
	 * Content/DataAssets 폴더를 기준으로 하는 JSON 상대 경로다.
	 *
	 * Content/DataAssets 자체는 입력하지 않는다.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "JSON Asset Sync|Binding",
		meta = (
			DisplayName = "JSON Relative Path",
			ToolTip = "Content/DataAssets 폴더를 기준으로 하는 JSON 상대 경로입니다."
			)
	)
	FString jsonRelativePath;

	/**
	 * JSON 값을 적용할 실제 DataAsset 에셋이다.
	 *
	 * UDataAsset을 상속한 여러 종류의 DataAsset을 선택할 수 있다.
	 * 실제 변환 시에는 선택된 DataAsset의 구체적인 클래스를 기준으로 검사한다.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "JSON Asset Sync|Binding",
		meta = (
			DisplayName = "Target Data Asset",
			ToolTip = "JSON 값을 적용할 대상 DataAsset 에셋입니다."
			)
	)
	TObjectPtr<UDataAsset> targetDataAsset = nullptr;
};

/**
 * 외부 JSON 파일과 프로젝트 내부 DataTable/DataAsset의 연결을 관리한다.
 *
 * Registry에 등록된 모든 항목은 자동 검사 및 적용 대상이다.
 * 사용 여부를 따로 관리하는 isEnabled 변수는 두지 않는다.
 *
 * Registry에 존재:
 * JSON 검사 및 적용 대상
 *
 * Registry에 존재하지 않음:
 * JSON 시스템에서 처리하지 않음
 */
UCLASS(BlueprintType)
class JSONASSETSYNC_API UJsonApplyRegistry : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Content/DataTables 내부의 JSON과 대상 DataTable 연결 목록이다.
	 *
	 * TitleProperty를 사용하여 배열 항목을 접었을 때도
	 * jsonRelativePath가 항목 제목으로 표시되게 한다.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "JSON Asset Sync|Bindings",
		meta = (
			DisplayName = "Data Table Bindings",
			TitleProperty = "jsonRelativePath"
			)
	)
	TArray<FJsonDataTableBinding> dataTableBindings;

	/**
	 * Content/DataAssets 내부의 JSON과 대상 DataAsset 연결 목록이다.
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadOnly,
		Category = "JSON Asset Sync|Bindings",
		meta = (
			DisplayName = "Data Asset Bindings",
			TitleProperty = "jsonRelativePath"
			)
	)
	TArray<FJsonDataAssetBinding> dataAssetBindings;
};