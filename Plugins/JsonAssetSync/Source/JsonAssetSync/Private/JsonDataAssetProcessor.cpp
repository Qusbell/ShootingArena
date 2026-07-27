// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonDataAssetProcessor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataAsset.h"
#include "JsonObjectConverter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace JsonAssetSync::DataAssetPrivate
{
	/**
	 * JSON 동기화 대상 프로퍼티 정보다.
	 */
	struct FSyncPropertyInfo
	{
		/** 실제 값을 읽고 쓸 FProperty다. */
		FProperty* property = nullptr;

		/**
		 * 기획자가 JSON에 작성할 필드명이다.
		 *
		 * GetAuthoredName을 사용하므로 Blueprint 내부 이름과 달라도
		 * 에디터에서 작성한 원래 변수명을 기준으로 사용할 수 있다.
		 */
		FString authoredJsonFieldName;

		/**
		 * FJsonObjectConverter에 전달할 내부 필드명이다.
		 *
		 * Blueprint에서 내부 프로퍼티 이름이 변형된 경우에도
		 * 변환기가 실제 FProperty를 찾을 수 있도록 별도로 보관한다.
		 */
		FString converterJsonFieldName;
	};

	/**
	 * DataAsset 처리 문제 한 건을 결과에 추가한다.
	 */
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
	 * 결과에 Error 등급 문제가 하나라도 존재하는지 확인한다.
	 */
	bool HasError(const FJsonApplyResult& result)
	{
		for (const FJsonApplyIssue& issue : result.issues)
		{
			if (issue.severity == EJsonApplyIssueSeverity::Error)
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * JSON 필드 비교에 사용할 대소문자 비구분 키를 만든다.
	 */
	FString MakeComparisonKey(const FString& inputName)
	{
		FString comparisonKey =
			FJsonObjectConverter::StandardizeCase(inputName);

		comparisonKey.ToLowerInline();

		return comparisonKey;
	}

	/**
	 * 엔진 기본 DataAsset 클래스가 자체적으로 보유한 내부 프로퍼티인지 확인한다.
	 *
	 * UDataAsset과 UPrimaryDataAsset의 엔진 내부 프로퍼티는
	 * 기획 데이터 JSON에 포함하도록 요구하지 않는다.
	 */
	bool IsEngineBaseDataAssetProperty(const FProperty* property)
	{
		if (property == nullptr)
		{
			return true;
		}

		const UStruct* ownerStruct = property->GetOwnerStruct();

		return ownerStruct == UObject::StaticClass() ||
			ownerStruct == UDataAsset::StaticClass() ||
			ownerStruct == UPrimaryDataAsset::StaticClass();
	}

	/**
	 * 프로퍼티가 JSON 동기화 기본 대상인지 확인한다.
	 */
	bool IsEditableSyncProperty(FProperty* property)
	{
		if (property == nullptr)
		{
			return false;
		}

		/*
		 * EditAnywhere, EditDefaultsOnly 등 편집 가능한 UPROPERTY만
		 * 외부 JSON 동기화 대상으로 사용한다.
		 */
		if (!property->HasAnyPropertyFlags(CPF_Edit))
		{
			return false;
		}

		if (IsEngineBaseDataAssetProperty(property))
		{
			return false;
		}

		/*
		 * 런타임 데이터 원본으로 사용하지 않을 프로퍼티는 제외한다.
		 */
		if (property->HasAnyPropertyFlags(CPF_Transient) ||
			property->HasAnyPropertyFlags(CPF_EditorOnly) ||
			property->HasAnyPropertyFlags(CPF_Deprecated) ||
			property->HasAnyPropertyFlags(CPF_EditConst) ||
			property->HasAnyPropertyFlags(CPF_SkipSerialization))
		{
			return false;
		}

		return true;
	}

	/**
	 * 현재 범용 동기화에서 지원하지 않는 프로퍼티인지 확인한다.
	 *
	 * Instanced UObject는 소유권과 생명주기 처리가 별도로 필요하며,
	 * Delegate는 데이터 설정값이 아니므로 동기화하지 않는다.
	 */
	bool IsUnsupportedSyncProperty(FProperty* property)
	{
		if (property == nullptr)
		{
			return true;
		}

		if (property->ContainsInstancedObjectProperty())
		{
			return true;
		}

		if (CastField<FDelegateProperty>(property) != nullptr ||
			CastField<FMulticastDelegateProperty>(property) != nullptr)
		{
			return true;
		}

		return false;
	}

	/**
	 * 대상 DataAsset 클래스에서 동기화 가능한 프로퍼티 목록을 수집한다.
	 *
	 * C++ 또는 Blueprint로 만든 사용자 DataAsset 부모 클래스의
	 * 편집 가능한 프로퍼티도 함께 수집한다.
	 */
	bool GatherSyncProperties(
		UClass* dataAssetClass,
		TArray<FSyncPropertyInfo>& outSyncProperties,
		TMap<FString, int32>& outSupportedPropertiesByKey,
		TMap<FString, FString>& outUnsupportedPropertiesByKey,
		FJsonApplyResult& inOutResult
	)
	{
		if (!IsValid(dataAssetClass))
		{
			AddIssue(
				inOutResult,
				EJsonApplyIssueStage::Conversion,
				EJsonApplyIssueSeverity::Error,
				TEXT("대상 DataAsset 클래스가 유효하지 않습니다.")
			);

			return false;
		}

		for (TFieldIterator<FProperty> propertyIterator(dataAssetClass);
			propertyIterator;
			++propertyIterator)
		{
			FProperty* property = *propertyIterator;

			if (!IsEditableSyncProperty(property))
			{
				continue;
			}

			const FString authoredJsonFieldName =
				FJsonObjectConverter::StandardizeCase(
					property->GetAuthoredName()
				);

			const FString converterJsonFieldName =
				FJsonObjectConverter::StandardizeCase(
					property->GetName()
				);

			const FString authoredComparisonKey =
				MakeComparisonKey(authoredJsonFieldName);

			const FString converterComparisonKey =
				MakeComparisonKey(converterJsonFieldName);

			if (IsUnsupportedSyncProperty(property))
			{
				outUnsupportedPropertiesByKey.Add(
					authoredComparisonKey,
					authoredJsonFieldName
				);

				outUnsupportedPropertiesByKey.Add(
					converterComparisonKey,
					authoredJsonFieldName
				);

				continue;
			}

			if (outSupportedPropertiesByKey.Contains(
				authoredComparisonKey
			))
			{
				AddIssue(
					inOutResult,
					EJsonApplyIssueStage::Conversion,
					EJsonApplyIssueSeverity::Error,
					FString::Printf(
						TEXT(
							"JSON 필드명이 서로 충돌하는 DataAsset "
							"프로퍼티가 있습니다: %s"
						),
						*authoredJsonFieldName
					),
					authoredJsonFieldName
				);

				continue;
			}

			FSyncPropertyInfo propertyInfo;
			propertyInfo.property = property;
			propertyInfo.authoredJsonFieldName =
				authoredJsonFieldName;
			propertyInfo.converterJsonFieldName =
				converterJsonFieldName;

			const int32 propertyIndex =
				outSyncProperties.Add(
					MoveTemp(propertyInfo)
				);

			/*
			 * JSON에는 사용자가 작성한 원래 변수명과
			 * 내부 프로퍼티 이름 중 어느 쪽이 들어와도 찾을 수 있게 한다.
			 *
			 * 정상적인 JSON 작성 기준은 authoredJsonFieldName이다.
			 */
			outSupportedPropertiesByKey.Add(
				authoredComparisonKey,
				propertyIndex
			);

			outSupportedPropertiesByKey.Add(
				converterComparisonKey,
				propertyIndex
			);
		}

		return !HasError(inOutResult);
	}

	/**
	 * JSON 필드 구조를 검사하고 변환기에 전달할 Object를 만든다.
	 *
	 * Strict Validation:
	 * - 알 수 없는 필드 오류
	 * - 지원하지 않는 필드 오류
	 * - 편집 가능한 필드 누락 오류
	 *
	 * Non-Strict Validation:
	 * - 알 수 없는 필드와 지원하지 않는 필드는 경고 후 무시
	 * - 누락 필드는 기존 DataAsset 값을 유지
	 */
	bool BuildFilteredJsonObject(
		const TSharedPtr<FJsonObject>& sourceJsonObject,
		const TArray<FSyncPropertyInfo>& syncProperties,
		const TMap<FString, int32>& supportedPropertiesByKey,
		const TMap<FString, FString>& unsupportedPropertiesByKey,
		const bool strictValidation,
		TSharedRef<FJsonObject>& outFilteredJsonObject,
		FJsonApplyResult& inOutResult
	)
	{
		if (!sourceJsonObject.IsValid())
		{
			AddIssue(
				inOutResult,
				EJsonApplyIssueStage::Structure,
				EJsonApplyIssueSeverity::Error,
				TEXT("DataAsset JSON Object가 유효하지 않습니다.")
			);

			return false;
		}

		TSet<FString> seenJsonKeys;
		TSet<int32> providedPropertyIndexes;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& jsonField :
			sourceJsonObject->Values)
		{
			const FString comparisonKey =
				MakeComparisonKey(jsonField.Key);

			/*
			 * 대소문자 차이만 있는 중복 필드는 항상 오류로 처리한다.
			 *
			 * 예:
			 * maxCount
			 * MaxCount
			 */
			if (seenJsonKeys.Contains(comparisonKey))
			{
				AddIssue(
					inOutResult,
					EJsonApplyIssueStage::Structure,
					EJsonApplyIssueSeverity::Error,
					FString::Printf(
						TEXT(
							"대소문자 차이만 있는 중복 JSON 필드가 "
							"발견됐습니다: %s"
						),
						*jsonField.Key
					),
					jsonField.Key
				);

				continue;
			}

			seenJsonKeys.Add(comparisonKey);

			if (const int32* propertyIndex =
				supportedPropertiesByKey.Find(comparisonKey))
			{
				if (!syncProperties.IsValidIndex(*propertyIndex))
				{
					AddIssue(
						inOutResult,
						EJsonApplyIssueStage::Conversion,
						EJsonApplyIssueSeverity::Error,
						TEXT(
							"내부 프로퍼티 인덱스가 올바르지 않습니다."
						),
						jsonField.Key
					);

					continue;
				}

				const FSyncPropertyInfo& propertyInfo =
					syncProperties[*propertyIndex];

				/*
				 * FJsonObjectConverter가 실제 FProperty를 찾을 수 있도록
				 * 내부 프로퍼티 기준의 필드명으로 변환해서 저장한다.
				 */
				outFilteredJsonObject->SetField(
					propertyInfo.converterJsonFieldName,
					jsonField.Value
				);

				providedPropertyIndexes.Add(*propertyIndex);
				continue;
			}

			if (const FString* unsupportedFieldName =
				unsupportedPropertiesByKey.Find(comparisonKey))
			{
				const EJsonApplyIssueSeverity severity =
					strictValidation
						? EJsonApplyIssueSeverity::Error
						: EJsonApplyIssueSeverity::Warning;

				AddIssue(
					inOutResult,
					EJsonApplyIssueStage::Structure,
					severity,
					FString::Printf(
						TEXT(
							"현재 범용 DataAsset 동기화에서 지원하지 "
							"않는 프로퍼티입니다: %s"
						),
						**unsupportedFieldName
					),
					jsonField.Key
				);

				continue;
			}

			const EJsonApplyIssueSeverity severity =
				strictValidation
					? EJsonApplyIssueSeverity::Error
					: EJsonApplyIssueSeverity::Warning;

			AddIssue(
				inOutResult,
				EJsonApplyIssueStage::Structure,
				severity,
				FString::Printf(
					TEXT(
						"대상 DataAsset에 존재하지 않는 JSON "
						"필드입니다: %s"
					),
					*jsonField.Key
				),
				jsonField.Key
			);
		}

		/*
		 * Strict Validation에서는 동기화 가능한 모든 프로퍼티가
		 * JSON에 포함되어 있어야 한다.
		 */
		if (strictValidation)
		{
			for (int32 propertyIndex = 0;
				propertyIndex < syncProperties.Num();
				++propertyIndex)
			{
				if (providedPropertyIndexes.Contains(propertyIndex))
				{
					continue;
				}

				const FSyncPropertyInfo& propertyInfo =
					syncProperties[propertyIndex];

				AddIssue(
					inOutResult,
					EJsonApplyIssueStage::Structure,
					EJsonApplyIssueSeverity::Error,
					FString::Printf(
						TEXT(
							"엄격 검사에서 필요한 DataAsset "
							"필드가 누락됐습니다: %s"
						),
						*propertyInfo.authoredJsonFieldName
					),
					propertyInfo.authoredJsonFieldName
				);
			}
		}

		return !HasError(inOutResult);
	}
}

bool FJsonDataAssetProcessor::ValidateAndApply(
	UDataAsset* targetDataAsset,
	const TSharedPtr<FJsonObject>& jsonObject,
	const bool strictValidation,
	FJsonApplyResult& inOutResult
)
{
	using namespace JsonAssetSync::DataAssetPrivate;

	inOutResult.wasApplied = false;

	/*
	 * Registry 단계에서도 확인하지만 실제 변환 직전에
	 * 대상 객체가 여전히 유효한지 다시 검사한다.
	 */
	if (!IsValid(targetDataAsset))
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT("적용할 DataAsset이 유효하지 않습니다.")
		);

		return false;
	}

	UClass* dataAssetClass =
		targetDataAsset->GetClass();

	if (!IsValid(dataAssetClass))
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT("대상 DataAsset의 클래스를 가져올 수 없습니다.")
		);

		return false;
	}

	TArray<FSyncPropertyInfo> syncProperties;
	TMap<FString, int32> supportedPropertiesByKey;
	TMap<FString, FString> unsupportedPropertiesByKey;

	if (!GatherSyncProperties(
		dataAssetClass,
		syncProperties,
		supportedPropertiesByKey,
		unsupportedPropertiesByKey,
		inOutResult
	))
	{
		return false;
	}

	if (syncProperties.IsEmpty())
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT(
				"대상 DataAsset에 JSON으로 동기화할 수 있는 "
				"편집 가능 프로퍼티가 없습니다."
			)
		);

		return false;
	}

	/*
	 * 실제 대상 DataAsset을 메모리 전용 임시 객체로 복제한다.
	 *
	 * 모든 JSON 변환은 이 복제본에만 수행되므로,
	 * 변환 실패 시 실제 대상은 전혀 변경되지 않는다.
	 *
	 * Strict Validation이 꺼져 있을 때 JSON에 없는 프로퍼티는
	 * 복제본에 들어 있는 기존 값을 그대로 유지한다.
	 */
	UDataAsset* stagedDataAsset =
		DuplicateObject<UDataAsset>(
			targetDataAsset,
			GetTransientPackage(),
			NAME_None
		);

	if (!IsValid(stagedDataAsset))
	{
		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			TEXT("검사용 임시 DataAsset을 복제하지 못했습니다.")
		);

		return false;
	}

	stagedDataAsset->SetFlags(RF_Transient);

	TSharedRef<FJsonObject> filteredJsonObject =
		MakeShared<FJsonObject>();

	if (!BuildFilteredJsonObject(
		jsonObject,
		syncProperties,
		supportedPropertiesByKey,
		unsupportedPropertiesByKey,
		strictValidation,
		filteredJsonObject,
		inOutResult
	))
	{
		return false;
	}

	/*
	 * 편집 가능한 프로퍼티만 변환하고,
	 * 런타임 동기화 대상이 아닌 프로퍼티는 제외한다.
	 */
	const int64 checkFlags =
		static_cast<int64>(CPF_Edit);

	const int64 skipFlags =
		static_cast<int64>(
			CPF_Transient |
			CPF_EditorOnly |
			CPF_Deprecated |
			CPF_EditConst |
			CPF_SkipSerialization
		);

	FText conversionFailureReason;

	/*
	 * UClass는 UStruct를 상속하므로 동적 UStruct 변환 오버로드를 사용하여
	 * 같은 클래스의 임시 DataAsset 객체에 JSON 값을 입력할 수 있다.
	 */
	const bool conversionSucceeded =
		FJsonObjectConverter::JsonObjectToUStruct(
			filteredJsonObject,
			dataAssetClass,
			stagedDataAsset,
			checkFlags,
			skipFlags,
			strictValidation,
			&conversionFailureReason,
			nullptr
		);

	if (!conversionSucceeded)
	{
		const FString failureMessage =
			conversionFailureReason.IsEmpty()
				? TEXT(
					"JSON 값을 DataAsset 프로퍼티로 "
					"변환하는 데 실패했습니다."
				)
				: conversionFailureReason.ToString();

		AddIssue(
			inOutResult,
			EJsonApplyIssueStage::Conversion,
			EJsonApplyIssueSeverity::Error,
			failureMessage
		);

		return false;
	}

	/*
	 * 이 지점까지 실제 대상 DataAsset은 변경되지 않았다.
	 *
	 * 모든 구조 검사와 타입 변환이 완료됐으므로,
	 * 검증된 임시 객체의 프로퍼티를 실제 대상에 복사한다.
	 */
	for (const FSyncPropertyInfo& propertyInfo :
		syncProperties)
	{
		if (propertyInfo.property == nullptr)
		{
			continue;
		}

		propertyInfo.property->CopyCompleteValue_InContainer(
			targetDataAsset,
			stagedDataAsset
		);
	}

	inOutResult.wasApplied = true;

	return true;
}
