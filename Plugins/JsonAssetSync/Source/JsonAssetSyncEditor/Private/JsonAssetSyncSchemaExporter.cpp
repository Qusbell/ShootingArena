// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonAssetSyncSchemaExporter.h"

#include "JsonApplyRegistry.h"
#include "JsonAssetSyncSettings.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataAsset.h"
#include "Engine/CurveTable.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "Curves/SimpleCurve.h"
#include "Engine/DataTable.h"
#include "DataTableUtils.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Field.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace JsonAssetSyncSchemaExporter::Private
{
	/** 외부 편집기가 항상 같은 위치에서 찾는 프로젝트 기준 Manifest 경로다. */
	const TCHAR* manifestRelativePath =
		TEXT("ExternalData/JsonAssetSyncManifest.json");

	/**
	 * CurveTable Manifest에 기록할 대표 보간 모드를 결정한다.
	 *
	 * 외부 편집기에서 참고용으로 사용할 값이며,
	 * 실제 JSON 적용 시에도 같은 판단 규칙을 사용한다.
	 */
	ERichCurveInterpMode ResolveCurveTableInterpMode(
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

	/**
	 * DataAsset JSON 동기화 대상 여부를 기존 Processor와 동일한 기준으로 판단한다.
	 */
	bool IsEditableDataAssetProperty(const FProperty* property)
	{
		if (property == nullptr || !property->HasAnyPropertyFlags(CPF_Edit))
		{
			return false;
		}

		const UStruct* ownerStruct = property->GetOwnerStruct();

		if (ownerStruct == UObject::StaticClass() ||
			ownerStruct == UDataAsset::StaticClass() ||
			ownerStruct == UPrimaryDataAsset::StaticClass())
		{
			return false;
		}

		if (property->HasAnyPropertyFlags(CPF_Transient) ||
			property->HasAnyPropertyFlags(CPF_EditorOnly) ||
			property->HasAnyPropertyFlags(CPF_Deprecated) ||
			property->HasAnyPropertyFlags(CPF_EditConst) ||
			property->HasAnyPropertyFlags(CPF_SkipSerialization))
		{
			return false;
		}

		if (property->ContainsInstancedObjectProperty() ||
			CastField<FDelegateProperty>(property) != nullptr ||
			CastField<FMulticastDelegateProperty>(property) != nullptr)
		{
			return false;
		}

		return true;
	}

	/**
	 * 외부 JSON에서 사용하는 필드명과 Manifest의 UI 정보를 공통으로 채운다.
	 */
	void AddCommonPropertyMetadata(
		const FProperty* property,
		const TSharedRef<FJsonObject>& propertyJson,
		const int32 order,
		const bool includeCategory
	)
	{
		const FString authoredName =
			FJsonObjectConverter::StandardizeCase(
				property->GetAuthoredName()
			);

		propertyJson->SetStringField(TEXT("name"), authoredName);
		propertyJson->SetStringField(
			TEXT("displayName"),
			property->GetDisplayNameText().ToString()
		);
		propertyJson->SetStringField(
			TEXT("cppType"),
			property->GetCPPType()
		);
		propertyJson->SetNumberField(TEXT("order"), order);

		const FString toolTip = property->GetToolTipText().ToString();
		if (!toolTip.IsEmpty())
		{
			propertyJson->SetStringField(TEXT("tooltip"), toolTip);
		}

		if (includeCategory)
		{
			FString category = property->GetMetaData(TEXT("Category"));
			if (category.IsEmpty())
			{
				category = TEXT("General");
			}

			propertyJson->SetStringField(TEXT("category"), category);
		}

		propertyJson->SetBoolField(
			TEXT("advancedDisplay"),
			property->HasMetaData(TEXT("AdvancedDisplay"))
		);

		TSharedRef<FJsonObject> constraints = MakeShared<FJsonObject>();
		bool hasConstraint = false;

		auto AddConstraint =
			[&](const TCHAR* metadataKey, const TCHAR* jsonKey)
			{
				const FString value = property->GetMetaData(metadataKey);
				if (!value.IsEmpty())
				{
					constraints->SetStringField(jsonKey, value);
					hasConstraint = true;
				}
			};

		AddConstraint(TEXT("ClampMin"), TEXT("clampMin"));
		AddConstraint(TEXT("ClampMax"), TEXT("clampMax"));
		AddConstraint(TEXT("UIMin"), TEXT("uiMin"));
		AddConstraint(TEXT("UIMax"), TEXT("uiMax"));
		AddConstraint(TEXT("Multiple"), TEXT("multiple"));
		AddConstraint(TEXT("Units"), TEXT("units"));

		if (hasConstraint)
		{
			propertyJson->SetObjectField(TEXT("constraints"), constraints);
		}
	}

	/** Enum 선택지 정보를 외부 편집기용으로 변환한다. */
	void AddEnumValues(
		const UEnum* enumObject,
		const TSharedRef<FJsonObject>& propertyJson
	)
	{
		if (!IsValid(enumObject))
		{
			return;
		}

		TArray<TSharedPtr<FJsonValue>> enumValues;

		for (int32 index = 0; index < enumObject->NumEnums(); ++index)
		{
			if (enumObject->HasMetaData(TEXT("Hidden"), index))
			{
				continue;
			}

			const FString name = enumObject->GetNameStringByIndex(index);
			if (name.EndsWith(TEXT("_MAX")) || name.Equals(TEXT("MAX")))
			{
				continue;
			}

			TSharedRef<FJsonObject> enumValue = MakeShared<FJsonObject>();
			enumValue->SetStringField(TEXT("name"), name);
			enumValue->SetStringField(
				TEXT("displayName"),
				enumObject->GetDisplayNameTextByIndex(index).ToString()
			);

			enumValues.Add(MakeShared<FJsonValueObject>(enumValue));
		}

		propertyJson->SetArrayField(TEXT("enumValues"), enumValues);
	}

	TSharedRef<FJsonObject> BuildPropertySchema(
		const FProperty* property,
		int32 order,
		bool includeCategory
	);

	/** Struct 내부 프로퍼티들을 재귀적으로 Schema에 추가한다. */
	void AddStructProperties(
		const UStruct* structType,
		const TSharedRef<FJsonObject>& propertyJson
	)
	{
		TArray<TSharedPtr<FJsonValue>> childProperties;
		int32 childOrder = 0;

		for (TFieldIterator<FProperty> iterator(structType); iterator; ++iterator)
		{
			const FProperty* childProperty = *iterator;
			if (childProperty == nullptr ||
				childProperty->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
			{
				continue;
			}

			childProperties.Add(
				MakeShared<FJsonValueObject>(
					BuildPropertySchema(
						childProperty,
						childOrder++,
						false
					)
				)
			);
		}

		propertyJson->SetArrayField(TEXT("properties"), childProperties);
	}

	/**
	 * FProperty 종류를 재귀적으로 분석하여 외부 UI가 해석할 Schema를 만든다.
	 */
	TSharedRef<FJsonObject> BuildPropertySchema(
		const FProperty* property,
		const int32 order,
		const bool includeCategory
	)
	{
		TSharedRef<FJsonObject> propertyJson = MakeShared<FJsonObject>();
		AddCommonPropertyMetadata(property, propertyJson, order, includeCategory);

		if (CastField<FBoolProperty>(property) != nullptr)
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("bool"));
			return propertyJson;
		}

		if (const FEnumProperty* enumProperty = CastField<FEnumProperty>(property))
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("enum"));
			AddEnumValues(enumProperty->GetEnum(), propertyJson);
			return propertyJson;
		}

		if (const FByteProperty* byteProperty = CastField<FByteProperty>(property))
		{
			if (IsValid(byteProperty->Enum))
			{
				propertyJson->SetStringField(TEXT("type"), TEXT("enum"));
				AddEnumValues(byteProperty->Enum, propertyJson);
			}
			else
			{
				propertyJson->SetStringField(TEXT("type"), TEXT("uint8"));
			}
			return propertyJson;
		}

		if (CastField<FNumericProperty>(property) != nullptr)
		{
			/*
			 * int32, int64, uint16, float, double처럼
			 * 외부 편집기가 바로 이해할 수 있는 C++ 타입명을 사용한다.
			 */
			FString numericType = property->GetCPPType();
			numericType.ToLowerInline();
			propertyJson->SetStringField(TEXT("type"), numericType);
			return propertyJson;
		}

		if (CastField<FStrProperty>(property) != nullptr)
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("string"));
			return propertyJson;
		}

		if (CastField<FNameProperty>(property) != nullptr)
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("name"));
			return propertyJson;
		}

		if (CastField<FTextProperty>(property) != nullptr)
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("text"));
			return propertyJson;
		}

		if (const FStructProperty* structProperty = CastField<FStructProperty>(property))
		{
			const FString structName = structProperty->Struct->GetName();

			/*
			 * FJsonObjectConverter가 현재 프로젝트에서 사용하는 기본 Struct 표현과
			 * 완전히 동일하게 유지하기 위해 GameplayTag 같은 특수 Struct도
			 * 임의의 문자열 문법으로 바꾸지 않고 실제 Reflection 구조를 내보낸다.
			 */
			propertyJson->SetStringField(TEXT("type"), TEXT("struct"));
			propertyJson->SetStringField(TEXT("structType"), structName);
			propertyJson->SetStringField(
				TEXT("structPath"),
				structProperty->Struct->GetPathName()
			);
			AddStructProperties(structProperty->Struct, propertyJson);
			return propertyJson;
		}

		if (const FArrayProperty* arrayProperty = CastField<FArrayProperty>(property))
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("array"));
			propertyJson->SetObjectField(
				TEXT("element"),
				BuildPropertySchema(arrayProperty->Inner, 0, false)
			);
			return propertyJson;
		}

		if (const FSetProperty* setProperty = CastField<FSetProperty>(property))
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("set"));
			propertyJson->SetObjectField(
				TEXT("element"),
				BuildPropertySchema(setProperty->ElementProp, 0, false)
			);
			return propertyJson;
		}

		if (const FMapProperty* mapProperty = CastField<FMapProperty>(property))
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("map"));
			propertyJson->SetObjectField(
				TEXT("key"),
				BuildPropertySchema(mapProperty->KeyProp, 0, false)
			);
			propertyJson->SetObjectField(
				TEXT("value"),
				BuildPropertySchema(mapProperty->ValueProp, 0, false)
			);
			return propertyJson;
		}

		/* Class 계열은 Object 계열보다 먼저 검사해야 파생 타입을 정확히 구분할 수 있다. */
		if (const FSoftClassProperty* softClass = CastField<FSoftClassProperty>(property))
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("softClass"));
			propertyJson->SetStringField(
				TEXT("metaClass"),
				IsValid(softClass->MetaClass)
					? softClass->MetaClass->GetPathName()
					: FString()
			);
			return propertyJson;
		}

		if (const FClassProperty* classProperty = CastField<FClassProperty>(property))
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("class"));
			propertyJson->SetStringField(
				TEXT("metaClass"),
				IsValid(classProperty->MetaClass)
					? classProperty->MetaClass->GetPathName()
					: FString()
			);
			return propertyJson;
		}

		if (const FSoftObjectProperty* softObject = CastField<FSoftObjectProperty>(property))
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("softObject"));
			if (IsValid(softObject->PropertyClass))
			{
				propertyJson->SetStringField(
					TEXT("objectClass"),
					softObject->PropertyClass->GetPathName()
				);
			}
			return propertyJson;
		}

		if (const FObjectPropertyBase* objectProperty = CastField<FObjectPropertyBase>(property))
		{
			propertyJson->SetStringField(TEXT("type"), TEXT("object"));
			if (IsValid(objectProperty->PropertyClass))
			{
				propertyJson->SetStringField(
					TEXT("objectClass"),
					objectProperty->PropertyClass->GetPathName()
				);
			}
			return propertyJson;
		}

		propertyJson->SetStringField(TEXT("type"), TEXT("unsupported"));
		return propertyJson;
	}


	/** 외부 JSON 파일의 실제 절대경로를 만든다. */
	FString MakeAbsoluteJsonPath(
		const FString& rootDirectory,
		const FString& bindingRelativePath
	)
	{
		FString result = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(
				FPaths::ProjectContentDir(),
				rootDirectory,
				bindingRelativePath
			)
		);
		FPaths::NormalizeFilename(result);
		return result;
	}

	/** Registry 비교용으로 상대경로를 정규화한다. */
	FString MakePathComparisonKey(const FString& relativePath)
	{
		FString key = relativePath;
		FPaths::NormalizeFilename(key);
		key.ToLowerInline();
		return key;
	}

	/**
	 * Target Asset 이름을 기준으로 자동 JSON 상대경로를 만든다.
	 *
	 * 기본:
	 * DT_Weapon.json
	 *
	 * 같은 Binding 경로가 이미 사용 중이면:
	 * DT_Weapon_2.json
	 */
	FString MakeUniqueAutoRelativePath(
		const UObject* targetAsset,
		const TSet<FString>& usedPaths
	)
	{
		const FString safeBaseName =
			FPaths::MakeValidFileName(
				IsValid(targetAsset)
					? targetAsset->GetName()
					: TEXT("ExternalData")
			);

		int32 suffix = 1;

		while (true)
		{
			const FString candidate =
				suffix == 1
					? safeBaseName + TEXT(".json")
					: FString::Printf(
						TEXT("%s_%d.json"),
						*safeBaseName,
						suffix
					);

			if (!usedPaths.Contains(
				MakePathComparisonKey(candidate)
			))
			{
				return candidate;
			}

			++suffix;
		}
	}

	/** DataTable의 현재 Row 값을 현재 JsonAssetSync 규격 JSON으로 내보낸다. */
	bool BuildDataTableJsonText(
		const UDataTable* targetDataTable,
		FString& outJsonText,
		FString& outError
	)
	{
		if (!IsValid(targetDataTable))
		{
			outError =
				TEXT("자동 JSON 생성 대상 DataTable이 유효하지 않습니다.");
			return false;
		}

		if (targetDataTable->GetRowStruct() == nullptr)
		{
			outError = FString::Printf(
				TEXT("DataTable %s에 RowStruct가 없어 JSON을 자동 생성할 수 없습니다."),
				*targetDataTable->GetPathName()
			);
			return false;
		}

		outJsonText = targetDataTable->GetTableAsJSON(
			static_cast<EDataTableExportFlags>(0)
		);

		if (outJsonText.IsEmpty())
		{
			outError = FString::Printf(
				TEXT("DataTable %s를 JSON 문자열로 변환하지 못했습니다."),
				*targetDataTable->GetPathName()
			);
			return false;
		}

		return true;
	}


	/** CurveTable의 현재 Row/Key 값을 Unreal 기본 JSON 형식으로 내보낸다. */
	bool BuildCurveTableJsonText(
		const UCurveTable* targetCurveTable,
		FString& outJsonText,
		FString& outError
	)
	{
		if (!IsValid(targetCurveTable))
		{
			outError =
				TEXT("자동 JSON 생성 대상 CurveTable이 유효하지 않습니다.");
			return false;
		}

		outJsonText = targetCurveTable->GetTableAsJSON();

		if (outJsonText.IsEmpty())
		{
			outError = FString::Printf(
				TEXT("CurveTable %s를 JSON 문자열로 변환하지 못했습니다."),
				*targetCurveTable->GetPathName()
			);
			return false;
		}

		return true;
	}


	/**
	 * Curve Float의 FRichCurve 전체를 Reflection JSON으로 내보낸다.
	 *
	 * FRichCurve / FRichCurveKey의 UPROPERTY를 그대로 직렬화하므로
	 * Time/Value뿐 아니라 Interp/Tangent/Weight/Extrapolation 정보도
	 * 외부 편집기와 Round Trip할 수 있다.
	 */
	bool BuildFloatCurveJsonText(
		const UCurveFloat* targetFloatCurve,
		FString& outJsonText,
		FString& outError
	)
	{
		if (!IsValid(targetFloatCurve))
		{
			outError =
				TEXT("자동 JSON 생성 대상 Curve Float가 유효하지 않습니다.");
			return false;
		}

		if (!FJsonObjectConverter::UStructToJsonObjectString(
			targetFloatCurve->FloatCurve,
			outJsonText,
			0,
			0,
			0,
			nullptr,
			true
		))
		{
			outError = FString::Printf(
				TEXT(
					"Curve Float %s의 FRichCurve를 "
					"JSON 문자열로 변환하지 못했습니다."
				),
				*targetFloatCurve->GetPathName()
			);
			return false;
		}

		return true;
	}

	/** DataAsset의 현재 편집 가능 값을 현재 JsonAssetSync 규격 JSON으로 내보낸다. */
	bool BuildDataAssetJsonText(
		UDataAsset* targetDataAsset,
		FString& outJsonText,
		FString& outError
	)
	{
		if (!IsValid(targetDataAsset))
		{
			outError =
				TEXT("자동 JSON 생성 대상 DataAsset이 유효하지 않습니다.");
			return false;
		}

		UClass* dataAssetClass = targetDataAsset->GetClass();
		if (!IsValid(dataAssetClass))
		{
			outError = FString::Printf(
				TEXT("DataAsset %s의 Class가 유효하지 않습니다."),
				*targetDataAsset->GetPathName()
			);
			return false;
		}

		TSharedRef<FJsonObject> rootObject = MakeShared<FJsonObject>();

		for (TFieldIterator<FProperty> iterator(dataAssetClass);
			iterator;
			++iterator)
		{
			FProperty* property = *iterator;
			if (!IsEditableDataAssetProperty(property))
			{
				continue;
			}

			const void* valuePtr =
				property->ContainerPtrToValuePtr<void>(
					targetDataAsset
				);

			TSharedPtr<FJsonValue> jsonValue =
				FJsonObjectConverter::UPropertyToJsonValue(
					property,
					valuePtr,
					0,
					CPF_Transient |
						CPF_EditorOnly |
						CPF_Deprecated |
						CPF_EditConst |
						CPF_SkipSerialization,
					nullptr,
					nullptr,
					EJsonObjectConversionFlags::None
				);

			if (!jsonValue.IsValid())
			{
				outError = FString::Printf(
					TEXT(
						"DataAsset %s의 프로퍼티 %s를 "
						"JSON 값으로 변환하지 못했습니다."
					),
					*targetDataAsset->GetPathName(),
					*property->GetAuthoredName()
				);
				return false;
			}

			const FString authoredJsonFieldName =
				FJsonObjectConverter::StandardizeCase(
					property->GetAuthoredName()
				);

			rootObject->SetField(
				authoredJsonFieldName,
				jsonValue
			);
		}

		const TSharedRef<
			TJsonWriter<
				TCHAR,
				TPrettyJsonPrintPolicy<TCHAR>
			>
		> writer =
			TJsonWriterFactory<
				TCHAR,
				TPrettyJsonPrintPolicy<TCHAR>
			>::Create(&outJsonText);

		if (!FJsonSerializer::Serialize(rootObject, writer))
		{
			outError = FString::Printf(
				TEXT("DataAsset %s의 JSON 직렬화에 실패했습니다."),
				*targetDataAsset->GetPathName()
			);
			return false;
		}

		return true;
	}

	/** 새 JSON 파일을 UTF-8로 안전하게 작성한다. */
	bool SaveGeneratedJsonFile(
		const FString& absolutePath,
		const FString& jsonText,
		FString& outError
	)
	{
		const FString directory = FPaths::GetPath(absolutePath);

		if (!IFileManager::Get().MakeDirectory(*directory, true) &&
			!IFileManager::Get().DirectoryExists(*directory))
		{
			outError = FString::Printf(
				TEXT("자동 JSON 폴더를 생성하지 못했습니다: %s"),
				*directory
			);
			return false;
		}

		if (!FFileHelper::SaveStringToFile(
			jsonText,
			*absolutePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
		))
		{
			outError = FString::Printf(
				TEXT("자동 JSON 파일을 저장하지 못했습니다: %s"),
				*absolutePath
			);
			return false;
		}

		return true;
	}

	/** 자동으로 채운 Registry 경로를 .uasset에 실제 저장한다. */
	bool SaveRegistryAsset(
		UJsonApplyRegistry* registry,
		FString& outError
	)
	{
		if (!IsValid(registry))
		{
			outError =
				TEXT("자동 JSON 경로를 저장할 Registry가 유효하지 않습니다.");
			return false;
		}

		UPackage* package = registry->GetOutermost();
		if (!IsValid(package) || package == GetTransientPackage())
		{
			outError =
				TEXT("Registry의 저장 가능한 Package를 찾지 못했습니다.");
			return false;
		}

		FString packageFilename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(
			package->GetName(),
			packageFilename,
			FPackageName::GetAssetPackageExtension()
		))
		{
			outError = FString::Printf(
				TEXT("Registry Package 경로를 파일 경로로 변환하지 못했습니다: %s"),
				*package->GetName()
			);
			return false;
		}

		FPaths::NormalizeFilename(packageFilename);

		if (IFileManager::Get().FileExists(*packageFilename) &&
			IFileManager::Get().IsReadOnly(*packageFilename))
		{
			outError = FString::Printf(
				TEXT(
					"Registry 파일이 읽기 전용이라 자동 JSON 경로를 "
					"저장할 수 없습니다: %s"
				),
				*packageFilename
			);
			return false;
		}

		registry->MarkPackageDirty();

		FSavePackageArgs saveArgs;
		saveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		saveArgs.SaveFlags = SAVE_None;
		saveArgs.bWarnOfLongFilename = true;

		if (!UPackage::SavePackage(
			package,
			registry,
			*packageFilename,
			saveArgs
		))
		{
			outError = FString::Printf(
				TEXT("Registry Asset 저장에 실패했습니다: %s"),
				*packageFilename
			);
			return false;
		}

		return true;
	}

	/**
	 * JSON Relative Path가 비어 있는 Binding에 기본 경로를 자동 지정하고,
	 * 해당 JSON 파일이 없으면 Target Asset의 현재 값으로 새 JSON을 만든다.
	 *
	 * 사용자가 직접 경로를 입력한 Binding은 절대 변경하지 않는다.
	 */
	bool PrepareAutomaticBindingPathsAndJson(
		UJsonApplyRegistry* registry,
		const UJsonAssetSyncSettings* settings,
		FJsonAssetSyncManifestExportResult& inOutResult
	)
	{
		if (!IsValid(registry) || !IsValid(settings))
		{
			inOutResult.errors.Add(
				TEXT("자동 JSON 경로 준비에 필요한 Registry 또는 Settings가 유효하지 않습니다.")
			);
			return false;
		}

		TSet<FString> usedDataTablePaths;
		for (const FJsonDataTableBinding& binding :
			registry->dataTableBindings)
		{
			if (!binding.jsonRelativePath.IsEmpty())
			{
				usedDataTablePaths.Add(
					MakePathComparisonKey(
						binding.jsonRelativePath
					)
				);
			}
		}


		TSet<FString> usedCurveTablePaths;
		for (const FJsonCurveTableBinding& binding :
			registry->curveTableBindings)
		{
			if (!binding.jsonRelativePath.IsEmpty())
			{
				usedCurveTablePaths.Add(
					MakePathComparisonKey(
						binding.jsonRelativePath
					)
				);
			}
		}

		TSet<FString> usedFloatCurvePaths;
		for (const FJsonFloatCurveBinding& binding :
			registry->floatCurveBindings)
		{
			if (!binding.jsonRelativePath.IsEmpty())
			{
				usedFloatCurvePaths.Add(
					MakePathComparisonKey(
						binding.jsonRelativePath
					)
				);
			}
		}

		TSet<FString> usedDataAssetPaths;
		for (const FJsonDataAssetBinding& binding :
			registry->dataAssetBindings)
		{
			if (!binding.jsonRelativePath.IsEmpty())
			{
				usedDataAssetPaths.Add(
					MakePathComparisonKey(
						binding.jsonRelativePath
					)
				);
			}
		}

		bool registryChanged = false;

		for (FJsonDataTableBinding& binding :
			registry->dataTableBindings)
		{
			if (!binding.jsonRelativePath.IsEmpty() ||
				!IsValid(binding.targetDataTable))
			{
				continue;
			}

			const FString generatedRelativePath =
				MakeUniqueAutoRelativePath(
					binding.targetDataTable,
					usedDataTablePaths
				);

			const FString absolutePath =
				MakeAbsoluteJsonPath(
					settings->dataTableJsonDirectory,
					generatedRelativePath
				);

			if (!IFileManager::Get().FileExists(*absolutePath))
			{
				FString jsonText;
				FString error;

				if (!BuildDataTableJsonText(
					binding.targetDataTable,
					jsonText,
					error
				) ||
					!SaveGeneratedJsonFile(
						absolutePath,
						jsonText,
						error
					))
				{
					inOutResult.errors.Add(MoveTemp(error));
					continue;
				}

				++inOutResult.createdJsonFileCount;
			}

			if (!registryChanged)
			{
				registry->Modify();
			}

			binding.jsonRelativePath = generatedRelativePath;
			usedDataTablePaths.Add(
				MakePathComparisonKey(
					generatedRelativePath
				)
			);

			++inOutResult.autoFilledPathCount;
			registryChanged = true;
		}


		for (FJsonCurveTableBinding& binding :
			registry->curveTableBindings)
		{
			if (!binding.jsonRelativePath.IsEmpty() ||
				!IsValid(binding.targetCurveTable))
			{
				continue;
			}

			const FString generatedRelativePath =
				MakeUniqueAutoRelativePath(
					binding.targetCurveTable,
					usedCurveTablePaths
				);

			const FString absolutePath =
				MakeAbsoluteJsonPath(
					settings->curveTableJsonDirectory,
					generatedRelativePath
				);

			if (!IFileManager::Get().FileExists(*absolutePath))
			{
				FString jsonText;
				FString error;

				if (!BuildCurveTableJsonText(
					binding.targetCurveTable,
					jsonText,
					error
				) ||
					!SaveGeneratedJsonFile(
						absolutePath,
						jsonText,
						error
					))
				{
					inOutResult.errors.Add(MoveTemp(error));
					continue;
				}

				++inOutResult.createdJsonFileCount;
			}

			if (!registryChanged)
			{
				registry->Modify();
			}

			binding.jsonRelativePath = generatedRelativePath;
			usedCurveTablePaths.Add(
				MakePathComparisonKey(
					generatedRelativePath
				)
			);

			++inOutResult.autoFilledPathCount;
			registryChanged = true;
		}


		for (FJsonFloatCurveBinding& binding :
			registry->floatCurveBindings)
		{
			if (!binding.jsonRelativePath.IsEmpty() ||
				!IsValid(binding.targetFloatCurve))
			{
				continue;
			}

			const FString generatedRelativePath =
				MakeUniqueAutoRelativePath(
					binding.targetFloatCurve,
					usedFloatCurvePaths
				);

			const FString absolutePath =
				MakeAbsoluteJsonPath(
					settings->floatCurveJsonDirectory,
					generatedRelativePath
				);

			if (!IFileManager::Get().FileExists(*absolutePath))
			{
				FString jsonText;
				FString error;

				if (!BuildFloatCurveJsonText(
					binding.targetFloatCurve,
					jsonText,
					error
				) ||
					!SaveGeneratedJsonFile(
						absolutePath,
						jsonText,
						error
					))
				{
					inOutResult.errors.Add(MoveTemp(error));
					continue;
				}

				++inOutResult.createdJsonFileCount;
			}

			if (!registryChanged)
			{
				registry->Modify();
			}

			binding.jsonRelativePath = generatedRelativePath;
			usedFloatCurvePaths.Add(
				MakePathComparisonKey(
					generatedRelativePath
				)
			);

			++inOutResult.autoFilledPathCount;
			registryChanged = true;
		}

		for (FJsonDataAssetBinding& binding :
			registry->dataAssetBindings)
		{
			if (!binding.jsonRelativePath.IsEmpty() ||
				!IsValid(binding.targetDataAsset))
			{
				continue;
			}

			const FString generatedRelativePath =
				MakeUniqueAutoRelativePath(
					binding.targetDataAsset,
					usedDataAssetPaths
				);

			const FString absolutePath =
				MakeAbsoluteJsonPath(
					settings->dataAssetJsonDirectory,
					generatedRelativePath
				);

			if (!IFileManager::Get().FileExists(*absolutePath))
			{
				FString jsonText;
				FString error;

				if (!BuildDataAssetJsonText(
					binding.targetDataAsset,
					jsonText,
					error
				) ||
					!SaveGeneratedJsonFile(
						absolutePath,
						jsonText,
						error
					))
				{
					inOutResult.errors.Add(MoveTemp(error));
					continue;
				}

				++inOutResult.createdJsonFileCount;
			}

			if (!registryChanged)
			{
				registry->Modify();
			}

			binding.jsonRelativePath = generatedRelativePath;
			usedDataAssetPaths.Add(
				MakePathComparisonKey(
					generatedRelativePath
				)
			);

			++inOutResult.autoFilledPathCount;
			registryChanged = true;
		}

		if (inOutResult.errors.Num() > 0)
		{
			return false;
		}

		if (!registryChanged)
		{
			return true;
		}

		/* Details Panel과 에디터에 자동 입력된 경로 변경을 알린다. */
		registry->PostEditChange();

		FString saveError;
		if (!SaveRegistryAsset(registry, saveError))
		{
			inOutResult.errors.Add(MoveTemp(saveError));
			return false;
		}

		return true;
	}

	/** 프로젝트 Content 기준 루트 + Binding 상대 경로를 Manifest 상대경로로 만든다. */
	FString MakeSourcePath(
		const FString& rootDirectory,
		const FString& bindingRelativePath
	)
	{
		FString result = FPaths::Combine(
			TEXT("Content"),
			rootDirectory,
			bindingRelativePath
		);
		FPaths::NormalizeFilename(result);
		return result;
	}

	/** DataTable 한 개의 Manifest Entry를 만든다. */
	bool BuildDataTableEntry(
		const FJsonDataTableBinding& binding,
		const UJsonAssetSyncSettings* settings,
		TSharedRef<FJsonObject>& outEntry,
		FString& outError
	)
	{
		if (!IsValid(binding.targetDataTable))
		{
			outError = TEXT("Manifest 생성 중 유효하지 않은 DataTable Binding을 발견했습니다.");
			return false;
		}

		const UScriptStruct* rowStruct = binding.targetDataTable->GetRowStruct();
		if (!IsValid(rowStruct))
		{
			outError = FString::Printf(
				TEXT("DataTable %s에 RowStruct가 없습니다."),
				*binding.targetDataTable->GetPathName()
			);
			return false;
		}

		outEntry->SetStringField(TEXT("id"), binding.targetDataTable->GetPathName());
		outEntry->SetStringField(TEXT("displayName"), binding.targetDataTable->GetName());
		outEntry->SetStringField(TEXT("assetType"), TEXT("DataTable"));
		outEntry->SetStringField(
			TEXT("source"),
			MakeSourcePath(settings->dataTableJsonDirectory, binding.jsonRelativePath)
		);
		outEntry->SetStringField(TEXT("target"), binding.targetDataTable->GetPathName());
		outEntry->SetStringField(TEXT("rowStruct"), rowStruct->GetPathName());

		TArray<TSharedPtr<FJsonValue>> properties;
		int32 order = 0;
		for (TFieldIterator<FProperty> iterator(rowStruct); iterator; ++iterator)
		{
			const FProperty* property = *iterator;
			if (property == nullptr ||
				property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
			{
				continue;
			}

			properties.Add(
				MakeShared<FJsonValueObject>(
					BuildPropertySchema(property, order++, false)
				)
			);
		}

		outEntry->SetArrayField(TEXT("properties"), properties);
		return true;
	}


	/** CurveTable 한 개의 Manifest Entry를 만든다. */
	bool BuildCurveTableEntry(
		const FJsonCurveTableBinding& binding,
		const UJsonAssetSyncSettings* settings,
		TSharedRef<FJsonObject>& outEntry,
		FString& outError
	)
	{
		if (!IsValid(binding.targetCurveTable))
		{
			outError =
				TEXT("Manifest 생성 중 유효하지 않은 CurveTable Binding을 발견했습니다.");
			return false;
		}

		outEntry->SetStringField(
			TEXT("id"),
			binding.targetCurveTable->GetPathName()
		);
		outEntry->SetStringField(
			TEXT("displayName"),
			binding.targetCurveTable->GetName()
		);
		outEntry->SetStringField(
			TEXT("assetType"),
			TEXT("CurveTable")
		);
		outEntry->SetStringField(
			TEXT("source"),
			MakeSourcePath(
				settings->curveTableJsonDirectory,
				binding.jsonRelativePath
			)
		);
		outEntry->SetStringField(
			TEXT("target"),
			binding.targetCurveTable->GetPathName()
		);

		switch (binding.targetCurveTable->GetCurveTableMode())
		{
		case ECurveTableMode::SimpleCurves:
			outEntry->SetStringField(
				TEXT("curveTableMode"),
				TEXT("SimpleCurves")
			);
			break;

		case ECurveTableMode::RichCurves:
			outEntry->SetStringField(
				TEXT("curveTableMode"),
				TEXT("RichCurves")
			);
			break;

		case ECurveTableMode::Empty:
		default:
			outEntry->SetStringField(
				TEXT("curveTableMode"),
				TEXT("Empty")
			);
			break;
		}

		outEntry->SetNumberField(
			TEXT("interpMode"),
			static_cast<int32>(
				ResolveCurveTableInterpMode(
					binding.targetCurveTable
				)
			)
		);

		return true;
	}


	/** Curve Float 한 개의 Manifest Entry를 만든다. */
	bool BuildFloatCurveEntry(
		const FJsonFloatCurveBinding& binding,
		const UJsonAssetSyncSettings* settings,
		TSharedRef<FJsonObject>& outEntry,
		FString& outError
	)
	{
		if (!IsValid(binding.targetFloatCurve))
		{
			outError =
				TEXT("Manifest 생성 중 유효하지 않은 Float Curve Binding을 발견했습니다.");
			return false;
		}

		outEntry->SetStringField(
			TEXT("id"),
			binding.targetFloatCurve->GetPathName()
		);
		outEntry->SetStringField(
			TEXT("displayName"),
			binding.targetFloatCurve->GetName()
		);
		outEntry->SetStringField(
			TEXT("assetType"),
			TEXT("FloatCurve")
		);
		outEntry->SetStringField(
			TEXT("source"),
			MakeSourcePath(
				settings->floatCurveJsonDirectory,
				binding.jsonRelativePath
			)
		);
		outEntry->SetStringField(
			TEXT("target"),
			binding.targetFloatCurve->GetPathName()
		);

		return true;
	}

	/** DataAsset 한 개의 Manifest Entry를 만든다. */
	bool BuildDataAssetEntry(
		const FJsonDataAssetBinding& binding,
		const UJsonAssetSyncSettings* settings,
		TSharedRef<FJsonObject>& outEntry,
		FString& outError
	)
	{
		if (!IsValid(binding.targetDataAsset))
		{
			outError = TEXT("Manifest 생성 중 유효하지 않은 DataAsset Binding을 발견했습니다.");
			return false;
		}

		UClass* dataAssetClass = binding.targetDataAsset->GetClass();
		if (!IsValid(dataAssetClass))
		{
			outError = FString::Printf(
				TEXT("DataAsset %s의 Class가 유효하지 않습니다."),
				*binding.targetDataAsset->GetPathName()
			);
			return false;
		}

		outEntry->SetStringField(TEXT("id"), binding.targetDataAsset->GetPathName());
		outEntry->SetStringField(TEXT("displayName"), binding.targetDataAsset->GetName());
		outEntry->SetStringField(TEXT("assetType"), TEXT("DataAsset"));
		outEntry->SetStringField(
			TEXT("source"),
			MakeSourcePath(settings->dataAssetJsonDirectory, binding.jsonRelativePath)
		);
		outEntry->SetStringField(TEXT("target"), binding.targetDataAsset->GetPathName());
		outEntry->SetStringField(TEXT("class"), dataAssetClass->GetPathName());

		TArray<TSharedPtr<FJsonValue>> properties;
		int32 order = 0;
		for (TFieldIterator<FProperty> iterator(dataAssetClass); iterator; ++iterator)
		{
			const FProperty* property = *iterator;
			if (!IsEditableDataAssetProperty(property))
			{
				continue;
			}

			properties.Add(
				MakeShared<FJsonValueObject>(
					BuildPropertySchema(property, order++, true)
				)
			);
		}

		outEntry->SetArrayField(TEXT("properties"), properties);
		return true;
	}

	/** JSON Object를 사람이 확인하기 쉬운 Pretty JSON 문자열로 직렬화한다. */
	bool SerializeManifest(
		const TSharedRef<FJsonObject>& manifest,
		FString& outText
	)
	{
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&outText);

		return FJsonSerializer::Serialize(manifest, writer);
	}
}

FJsonAssetSyncManifestExportResult
FJsonAssetSyncSchemaExporter::ExportManifest(const bool forceRewrite)
{
	using namespace JsonAssetSyncSchemaExporter::Private;

	FJsonAssetSyncManifestExportResult result;
	result.manifestPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), manifestRelativePath)
	);
	FPaths::NormalizeFilename(result.manifestPath);

	const UJsonAssetSyncSettings* settings = GetDefault<UJsonAssetSyncSettings>();
	if (!IsValid(settings))
	{
		result.errors.Add(TEXT("JSON Asset Sync 설정을 가져오지 못했습니다."));
		return result;
	}

	UJsonApplyRegistry* registry = settings->registryAsset.LoadSynchronous();
	if (!IsValid(registry))
	{
		result.errors.Add(
			TEXT("Registry Asset이 지정되지 않았거나 로드할 수 없어 Manifest를 생성하지 못했습니다.")
		);
		return result;
	}

	/*
	 * Target만 지정하고 JSON Relative Path를 비워둔 경우
	 * 경로와 초기 JSON을 자동으로 준비한다.
	 *
	 * 사용자가 직접 작성한 경로는 절대 변경하지 않는다.
	 */
	if (!PrepareAutomaticBindingPathsAndJson(
		registry,
		settings,
		result
	))
	{
		return result;
	}

	TSharedRef<FJsonObject> manifest = MakeShared<FJsonObject>();
	manifest->SetNumberField(TEXT("manifestVersion"), 1);
	manifest->SetStringField(TEXT("projectName"), FApp::GetProjectName());
	manifest->SetStringField(TEXT("registry"), registry->GetPathName());

	TArray<TSharedPtr<FJsonValue>> dataTables;
	for (const FJsonDataTableBinding& binding : registry->dataTableBindings)
	{
		TSharedRef<FJsonObject> entry = MakeShared<FJsonObject>();
		FString error;
		if (!BuildDataTableEntry(binding, settings, entry, error))
		{
			result.errors.Add(MoveTemp(error));
			continue;
		}
		dataTables.Add(MakeShared<FJsonValueObject>(entry));
	}
	manifest->SetArrayField(TEXT("dataTables"), dataTables);


	TArray<TSharedPtr<FJsonValue>> curveTables;
	for (const FJsonCurveTableBinding& binding :
		registry->curveTableBindings)
	{
		TSharedRef<FJsonObject> entry =
			MakeShared<FJsonObject>();
		FString error;

		if (!BuildCurveTableEntry(
			binding,
			settings,
			entry,
			error
		))
		{
			result.errors.Add(MoveTemp(error));
			continue;
		}

		curveTables.Add(
			MakeShared<FJsonValueObject>(entry)
		);
	}
	manifest->SetArrayField(
		TEXT("curveTables"),
		curveTables
	);


	TArray<TSharedPtr<FJsonValue>> floatCurves;
	for (const FJsonFloatCurveBinding& binding :
		registry->floatCurveBindings)
	{
		TSharedRef<FJsonObject> entry =
			MakeShared<FJsonObject>();
		FString error;

		if (!BuildFloatCurveEntry(
			binding,
			settings,
			entry,
			error
		))
		{
			result.errors.Add(MoveTemp(error));
			continue;
		}

		floatCurves.Add(
			MakeShared<FJsonValueObject>(entry)
		);
	}
	manifest->SetArrayField(
		TEXT("floatCurves"),
		floatCurves
	);

	TArray<TSharedPtr<FJsonValue>> dataAssets;
	for (const FJsonDataAssetBinding& binding : registry->dataAssetBindings)
	{
		TSharedRef<FJsonObject> entry = MakeShared<FJsonObject>();
		FString error;
		if (!BuildDataAssetEntry(binding, settings, entry, error))
		{
			result.errors.Add(MoveTemp(error));
			continue;
		}
		dataAssets.Add(MakeShared<FJsonValueObject>(entry));
	}
	manifest->SetArrayField(TEXT("dataAssets"), dataAssets);

	/* 일부 Binding을 누락한 불완전 Manifest로 기존 정상 파일을 덮어쓰지 않는다. */
	if (result.errors.Num() > 0)
	{
		return result;
	}

	FString manifestText;
	if (!SerializeManifest(manifest, manifestText))
	{
		result.errors.Add(TEXT("Manifest JSON 직렬화에 실패했습니다."));
		return result;
	}

	FString existingText;
	const bool hasExistingFile = FFileHelper::LoadFileToString(
		existingText,
		*result.manifestPath
	);

	if (!forceRewrite && hasExistingFile && existingText == manifestText)
	{
		result.success = true;
		result.changed = false;
		return result;
	}

	const FString manifestDirectory = FPaths::GetPath(result.manifestPath);
	if (!IFileManager::Get().MakeDirectory(*manifestDirectory, true) &&
		!IFileManager::Get().DirectoryExists(*manifestDirectory))
	{
		result.errors.Add(
			FString::Printf(
				TEXT("Manifest 폴더를 생성하지 못했습니다: %s"),
				*manifestDirectory
			)
		);
		return result;
	}

	if (!FFileHelper::SaveStringToFile(
		manifestText,
		*result.manifestPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		result.errors.Add(
			FString::Printf(
				TEXT("Manifest 파일을 저장하지 못했습니다: %s"),
				*result.manifestPath
			)
		);
		return result;
	}

	result.success = true;
	result.changed = true;
	return result;
}

FJsonAssetSyncAssetExportResult
FJsonAssetSyncSchemaExporter::ExportSavedAssetJson(UObject* savedAsset)
{
	using namespace JsonAssetSyncSchemaExporter::Private;

	FJsonAssetSyncAssetExportResult result;

	if (!IsValid(savedAsset))
	{
		return result;
	}

	const UJsonAssetSyncSettings* settings = GetDefault<UJsonAssetSyncSettings>();
	if (!IsValid(settings))
	{
		return result;
	}

	UJsonApplyRegistry* registry = settings->registryAsset.LoadSynchronous();
	if (!IsValid(registry))
	{
		return result;
	}

	auto ExportJson =
		[&result](const FString& absolutePath, const FString& jsonText, FString& error)
		{
			if (!SaveGeneratedJsonFile(absolutePath, jsonText, error))
			{
				result.success = false;
				result.errors.Add(MoveTemp(error));
				return;
			}

			++result.updatedJsonFileCount;
		};

	for (const FJsonDataTableBinding& binding : registry->dataTableBindings)
	{
		if (binding.targetDataTable != savedAsset || binding.jsonRelativePath.IsEmpty())
		{
			continue;
		}

		result.wasHandled = true;
		FString jsonText;
		FString error;
		if (!BuildDataTableJsonText(binding.targetDataTable, jsonText, error))
		{
			result.success = false;
			result.errors.Add(MoveTemp(error));
			continue;
		}

		ExportJson(MakeAbsoluteJsonPath(settings->dataTableJsonDirectory, binding.jsonRelativePath), jsonText, error);
	}

	for (const FJsonCurveTableBinding& binding : registry->curveTableBindings)
	{
		if (binding.targetCurveTable != savedAsset || binding.jsonRelativePath.IsEmpty())
		{
			continue;
		}

		result.wasHandled = true;
		FString jsonText;
		FString error;
		if (!BuildCurveTableJsonText(binding.targetCurveTable, jsonText, error))
		{
			result.success = false;
			result.errors.Add(MoveTemp(error));
			continue;
		}

		ExportJson(MakeAbsoluteJsonPath(settings->curveTableJsonDirectory, binding.jsonRelativePath), jsonText, error);
	}

	for (const FJsonFloatCurveBinding& binding : registry->floatCurveBindings)
	{
		if (binding.targetFloatCurve != savedAsset || binding.jsonRelativePath.IsEmpty())
		{
			continue;
		}

		result.wasHandled = true;
		FString jsonText;
		FString error;
		if (!BuildFloatCurveJsonText(binding.targetFloatCurve, jsonText, error))
		{
			result.success = false;
			result.errors.Add(MoveTemp(error));
			continue;
		}

		ExportJson(MakeAbsoluteJsonPath(settings->floatCurveJsonDirectory, binding.jsonRelativePath), jsonText, error);
	}

	for (const FJsonDataAssetBinding& binding : registry->dataAssetBindings)
	{
		if (binding.targetDataAsset != savedAsset || binding.jsonRelativePath.IsEmpty())
		{
			continue;
		}

		result.wasHandled = true;
		FString jsonText;
		FString error;
		if (!BuildDataAssetJsonText(binding.targetDataAsset, jsonText, error))
		{
			result.success = false;
			result.errors.Add(MoveTemp(error));
			continue;
		}

		ExportJson(MakeAbsoluteJsonPath(settings->dataAssetJsonDirectory, binding.jsonRelativePath), jsonText, error);
	}

	return result;
}
