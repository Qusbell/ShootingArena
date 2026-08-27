// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JsonApplyTypes.generated.h"

/**
 * Unreal Editor에서 JSON 적용 결과를 처리하는 방식을 나타낸다.
 *
 * 패키징된 게임에서는 이 설정과 관계없이
 * 항상 런타임 메모리에만 JSON 값이 적용된다.
 */
UENUM(BlueprintType)
enum class EJsonEditorApplyMode : uint8
{
	/**
	 * JSON 값을 현재 실행 중인 메모리에만 적용한다.
	 *
	 * 원본 .uasset은 수정하거나 저장하지 않는다.
	 * JSON을 데이터 원본으로 사용할 때 사용하는 기본 방식이다.
	 */
	MemoryOnly UMETA(DisplayName = "Memory Only"),

	/**
	 * JSON 값을 에셋에 적용한 뒤
	 * 변경된 .uasset까지 저장한다.
	 *
	 * 소스 관리에 에셋 변경사항이 나타날 수 있으므로
	 * 명시적으로 필요한 상황에서만 사용한다.
	 */
	ApplyAndSave UMETA(DisplayName = "Apply And Save")
};

/**
 * JSON이 적용되는 대상의 종류다.
 *
 * 결과 로그와 에디터 검사 도구에서
 * DataTable과 DataAsset 결과를 구분할 때 사용한다.
 */
UENUM(BlueprintType)
enum class EJsonApplyTargetType : uint8
{
	/** 특정 대상이 아직 결정되지 않은 상태다. */
	Unknown UMETA(DisplayName = "Unknown"),

	/** 대상이 UDataTable인 경우다. */
	DataTable UMETA(DisplayName = "Data Table"),

	/** 대상이 UCurveTable인 경우다. */
	CurveTable UMETA(DisplayName = "Curve Table"),

	/** 대상이 UCurveFloat인 경우다. */
	FloatCurve UMETA(DisplayName = "Float Curve"),

	/** 대상이 UDataAsset 계열인 경우다. */
	DataAsset UMETA(DisplayName = "Data Asset")
};

/**
 * 문제가 발견된 처리 단계를 나타낸다.
 *
 * 오류 메시지만 보는 것보다 어느 단계에서 실패했는지를
 * 빠르게 구분할 수 있도록 별도로 저장한다.
 */
UENUM(BlueprintType)
enum class EJsonApplyIssueStage : uint8
{
	/** Registry 또는 Project Settings 설정 문제다. */
	Registry UMETA(DisplayName = "Registry"),

	/** 상대 경로 검증 또는 전체 경로 계산 문제다. */
	Path UMETA(DisplayName = "Path"),

	/** JSON 파일 존재 확인 또는 읽기 문제다. */
	FileRead UMETA(DisplayName = "File Read"),

	/** JSON 문법 분석에 실패한 문제다. */
	JsonParse UMETA(DisplayName = "JSON Parse"),

	/** JSON의 최상위 형식이나 기본 구조가 잘못된 문제다. */
	Structure UMETA(DisplayName = "Structure"),

	/** JSON 데이터를 Unreal 타입으로 변환하는 과정의 문제다. */
	Conversion UMETA(DisplayName = "Conversion"),

	/** 검사가 끝난 데이터를 실제 에셋에 적용하는 과정의 문제다. */
	Commit UMETA(DisplayName = "Commit"),

	/** 적용된 에셋 패키지를 디스크의 .uasset으로 저장하는 과정의 문제다. */
	AssetSave UMETA(DisplayName = "Asset Save")
};

/**
 * JSON 검사 또는 적용 과정에서 발견된 문제의 심각도다.
 */
UENUM(BlueprintType)
enum class EJsonApplyIssueSeverity : uint8
{
	/** 정상 처리 과정에서 참고용으로 제공되는 정보다. */
	Info UMETA(DisplayName = "Info"),

	/** 적용은 가능하지만 확인할 필요가 있는 문제다. */
	Warning UMETA(DisplayName = "Warning"),

	/** 해당 JSON을 대상 에셋에 적용할 수 없는 오류다. */
	Error UMETA(DisplayName = "Error")
};

/**
 * JSON 검사 또는 적용 과정에서 발생한 문제 한 건을 저장한다.
 *
 * 단순 문자열 로그로 끝내지 않고 구조체로 보관하여,
 * 추후 에디터 검사 창이나 Blueprint에서도 같은 결과를
 * 그대로 사용할 수 있도록 한다.
 */
USTRUCT(BlueprintType)
struct JSONASSETSYNC_API FJsonApplyIssue
{
	GENERATED_BODY()

public:
	/** 문제가 발견된 처리 단계다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	EJsonApplyIssueStage stage = EJsonApplyIssueStage::Registry;

	/** 문제의 심각도다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	EJsonApplyIssueSeverity severity = EJsonApplyIssueSeverity::Error;

	/**
	 * 문제가 발생한 JSON 파일 경로다.
	 *
	 * 경로 계산 전에 실패했다면 상대 경로가 들어갈 수 있고,
	 * 경로 계산 이후에는 전체 파일 경로가 들어간다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	FString sourceJsonPath;

	/**
	 * JSON이 적용될 대상 Unreal 에셋 경로다.
	 *
	 * 예:
	 * /Game/Project/Data/DT_Test.DT_Test
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	FString targetAssetPath;

	/**
	 * DataTable에서 문제가 발생한 Row Name이다.
	 *
	 * DataAsset 문제이거나 특정 Row와 관계없다면 None이다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	FName rowName = NAME_None;

	/**
	 * 문제가 발생한 프로퍼티 또는 중첩 데이터 경로다.
	 *
	 * 예:
	 * damage
	 * nestedData.value
	 * rewards[2].amount
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	FString propertyPath;

	/** 사람이 확인할 수 있는 구체적인 문제 설명이다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	FString message;
};

/**
 * JSON 파일 하나를 검사하고 처리한 결과다.
 *
 * 한 Binding마다 FJsonApplyResult 하나가 생성된다.
 */
USTRUCT(BlueprintType)
struct JSONASSETSYNC_API FJsonApplyResult
{
	GENERATED_BODY()

public:
	/** 현재 대상이 DataTable인지 DataAsset인지를 나타낸다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	EJsonApplyTargetType targetType = EJsonApplyTargetType::Unknown;

	/**
	 * 현재 단계까지 모든 검사를 통과했는지를 나타낸다.
	 *
	 * 다음 단계에서 실제 적용 기능이 추가되면,
	 * 검사와 적용을 모두 통과해야 true가 된다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	bool isSuccess = false;

	/**
	 * 실제 대상 에셋의 메모리 값까지 변경했는지를 나타낸다.
	 *
	 * 현재 검사 단계에서는 항상 false다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	bool wasApplied = false;

	/**
	 * 에디터의 Apply And Save 모드에서
	 * 변경된 대상 .uasset 저장까지 성공했는지를 나타낸다.
	 *
	 * Memory Only와 패키징 게임에서는 항상 false다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	bool wasSaved = false;

	/** 검사한 JSON 파일 경로다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	FString sourceJsonPath;

	/** 적용 대상 Unreal 에셋 경로다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	FString targetAssetPath;

	/** 해당 JSON을 처리하며 발견된 정보, 경고, 오류 목록이다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Result")
	TArray<FJsonApplyIssue> issues;
};

/**
 * Registry 전체를 처리한 최종 요약 정보다.
 */
USTRUCT(BlueprintType)
struct JSONASSETSYNC_API FJsonApplySummary
{
	GENERATED_BODY()

public:
	/**
	 * Project Settings와 Registry를 정상적으로 읽었는지를 나타낸다.
	 *
	 * false라면 개별 Binding 처리 전에 시스템 설정 단계에서 실패한 것이다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Summary")
	bool isSystemReady = false;

	/** 검사 대상으로 확인한 전체 Binding 개수다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Summary")
	int32 totalCount = 0;

	/** 현재 처리 단계를 정상적으로 통과한 Binding 개수다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Summary")
	int32 successCount = 0;

	/** 검사 또는 처리에 실패한 Binding 개수다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Summary")
	int32 failureCount = 0;

	/**
	 * 특정 Binding이 아니라 시스템 전체 설정과 관련된 문제 목록이다.
	 *
	 * Registry가 지정되지 않은 경우 등이 여기에 들어간다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Summary")
	TArray<FJsonApplyIssue> globalIssues;

	/** 각 JSON Binding의 상세 처리 결과다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JSON Asset Sync|Summary")
	TArray<FJsonApplyResult> results;
};