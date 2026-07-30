// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonApplyService.h"

#include "JsonApplyRegistry.h"
#include "JsonAssetSyncLog.h"
#include "JsonAssetSyncSettings.h"
#include "JsonDataAssetProcessor.h"
#include "JsonDataTableProcessor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace JsonAssetSync::Private
{
	/**
	 * JSON 최상위에서 기대하는 형식이다.
	 */
	enum class EExpectedJsonRootType : uint8
	{
		Array,
		Object
	};

	/**
	 * 읽기와 문법 분석이 완료된 JSON 데이터다.
	 */
	struct FLoadedJsonDocument
	{
		/** 파일에서 읽은 원본 JSON 문자열이다. */
		FString jsonText;

		/** 문법 분석이 끝난 최상위 JSON 값이다. */
		TSharedPtr<FJsonValue> rootValue;
	};

	/**
	 * 문제 구조체를 생성한다.
	 */
	FJsonApplyIssue MakeIssue(
		const EJsonApplyIssueStage stage,
		const EJsonApplyIssueSeverity severity,
		const FString& sourceJsonPath,
		const FString& targetAssetPath,
		const FString& message,
		const FName rowName = NAME_None,
		const FString& propertyPath = FString()
	)
	{
		FJsonApplyIssue issue;

		issue.stage = stage;
		issue.severity = severity;
		issue.sourceJsonPath = sourceJsonPath;
		issue.targetAssetPath = targetAssetPath;
		issue.rowName = rowName;
		issue.propertyPath = propertyPath;
		issue.message = message;

		return issue;
	}

	/**
	 * 결과에 Error가 하나라도 있는지 확인한다.
	 */
	bool HasError(const FJsonApplyResult& result)
	{
		for (const FJsonApplyIssue& issue : result.issues)
		{
			if (issue.severity ==
				EJsonApplyIssueSeverity::Error)
			{
				return true;
			}
		}

		return false;
	}

#if WITH_EDITOR
	/**
	 * 메모리 적용이 끝난 에셋의 패키지를 .uasset으로 저장한다.
	 *
	 * Runtime 모듈 안에 있지만 WITH_EDITOR에서만 컴파일되므로
	 * 패키징 게임에는 저장 코드가 포함되지 않는다.
	 */
	bool SaveAppliedAsset(
		UObject* targetAsset,
		FJsonApplyResult& inOutResult
	)
	{
		inOutResult.wasSaved = false;

		if (!IsValid(targetAsset))
		{
			inOutResult.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::AssetSave,
					EJsonApplyIssueSeverity::Error,
					inOutResult.sourceJsonPath,
					inOutResult.targetAssetPath,
					TEXT("저장할 대상 에셋이 유효하지 않습니다.")
				)
			);

			return false;
		}

		UPackage* package = targetAsset->GetOutermost();

		if (!IsValid(package) || package == GetTransientPackage())
		{
			inOutResult.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::AssetSave,
					EJsonApplyIssueSeverity::Error,
					inOutResult.sourceJsonPath,
					inOutResult.targetAssetPath,
					TEXT("대상 에셋의 저장 가능한 패키지를 찾지 못했습니다.")
				)
			);

			return false;
		}

		const FString packageName = package->GetName();
		FString packageFilename;

		if (!FPackageName::TryConvertLongPackageNameToFilename(
			packageName,
			packageFilename,
			FPackageName::GetAssetPackageExtension()
		))
		{
			inOutResult.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::AssetSave,
					EJsonApplyIssueSeverity::Error,
					inOutResult.sourceJsonPath,
					inOutResult.targetAssetPath,
					FString::Printf(
						TEXT("패키지 경로를 .uasset 파일 경로로 변환하지 못했습니다: %s"),
						*packageName
					)
				)
			);

			return false;
		}

		FPaths::NormalizeFilename(packageFilename);

		if (IFileManager::Get().FileExists(*packageFilename) &&
			IFileManager::Get().IsReadOnly(*packageFilename))
		{
			inOutResult.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::AssetSave,
					EJsonApplyIssueSeverity::Error,
					inOutResult.sourceJsonPath,
					inOutResult.targetAssetPath,
					FString::Printf(
						TEXT("에셋 파일이 읽기 전용이라 저장할 수 없습니다. 소스 컨트롤 Checkout 또는 파일 쓰기 권한을 확인하세요: %s"),
						*packageFilename
					)
				)
			);

			return false;
		}

		/*
		 * JSON 적용은 이미 끝났으므로 패키지를 Dirty로 표시한 뒤
		 * 현재 메모리 상태를 원본 .uasset에 직렬화한다.
		 */
		targetAsset->MarkPackageDirty();

		FSavePackageArgs saveArgs;
		saveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		saveArgs.SaveFlags = SAVE_None;
		saveArgs.bWarnOfLongFilename = true;

		const bool saveSucceeded =
			UPackage::SavePackage(
				package,
				targetAsset,
				*packageFilename,
				saveArgs
			);

		if (!saveSucceeded)
		{
			inOutResult.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::AssetSave,
					EJsonApplyIssueSeverity::Error,
					inOutResult.sourceJsonPath,
					inOutResult.targetAssetPath,
					FString::Printf(
						TEXT("JSON 값은 메모리에 적용됐지만 .uasset 저장에 실패했습니다: %s"),
						*packageFilename
					)
				)
			);

			return false;
		}

		inOutResult.wasSaved = true;

		inOutResult.issues.Add(
			MakeIssue(
				EJsonApplyIssueStage::AssetSave,
				EJsonApplyIssueSeverity::Info,
				inOutResult.sourceJsonPath,
				inOutResult.targetAssetPath,
				FString::Printf(
					TEXT("변경된 에셋을 저장했습니다: %s"),
					*packageFilename
				)
			)
		);

		return true;
	}
#endif

	/**
	 * Registry의 JSON 상대 경로를 검사하고 정규화한다.
	 */
	bool NormalizeJsonRelativePath(
		const FString& inputPath,
		FString& outNormalizedPath,
		FString& outErrorMessage
	)
	{
		outNormalizedPath =
			inputPath.TrimStartAndEnd();

		outNormalizedPath.ReplaceInline(
			TEXT("\\"),
			TEXT("/")
		);

		if (outNormalizedPath.IsEmpty())
		{
			outErrorMessage =
				TEXT("JSON 상대 경로가 비어 있습니다.");

			return false;
		}

		if (!FPaths::IsRelative(outNormalizedPath))
		{
			outErrorMessage =
				TEXT(
					"JSON 경로는 Content의 JSON 루트 폴더를 "
					"기준으로 하는 상대 경로여야 합니다."
				);

			return false;
		}

		TArray<FString> pathComponents;

		outNormalizedPath.ParseIntoArray(
			pathComponents,
			TEXT("/"),
			false
		);

		for (const FString& component : pathComponents)
		{
			if (component == TEXT(".."))
			{
				outErrorMessage =
					TEXT(
						"JSON 상대 경로에 상위 폴더 이동을 의미하는 "
						"'..'를 사용할 수 없습니다."
					);

				return false;
			}
		}

		const FString extension =
			FPaths::GetExtension(
				outNormalizedPath,
				false
			);

		if (!extension.Equals(
			TEXT("json"),
			ESearchCase::IgnoreCase
		))
		{
			outErrorMessage =
				TEXT(
					"Registry의 JSON 상대 경로는 "
					".json 확장자를 사용해야 합니다."
				);

			return false;
		}

		FPaths::NormalizeFilename(outNormalizedPath);
		FPaths::CollapseRelativeDirectories(
			outNormalizedPath
		);

		return true;
	}

	/**
	 * Project Settings의 JSON 루트 디렉터리를 검사한다.
	 */
	bool NormalizeRootDirectory(
		const FString& inputDirectory,
		FString& outNormalizedDirectory,
		FString& outErrorMessage
	)
	{
		outNormalizedDirectory =
			inputDirectory.TrimStartAndEnd();

		outNormalizedDirectory.ReplaceInline(
			TEXT("\\"),
			TEXT("/")
		);

		if (outNormalizedDirectory.IsEmpty())
		{
			outErrorMessage =
				TEXT("JSON 루트 디렉터리가 비어 있습니다.");

			return false;
		}

		if (!FPaths::IsRelative(outNormalizedDirectory))
		{
			outErrorMessage =
				TEXT(
					"JSON 루트 디렉터리는 프로젝트 Content 폴더 "
					"기준의 상대 경로여야 합니다."
				);

			return false;
		}

		TArray<FString> pathComponents;

		outNormalizedDirectory.ParseIntoArray(
			pathComponents,
			TEXT("/"),
			false
		);

		for (const FString& component : pathComponents)
		{
			if (component == TEXT(".."))
			{
				outErrorMessage =
					TEXT(
						"JSON 루트 디렉터리에 상위 폴더 이동을 의미하는 "
						"'..'를 사용할 수 없습니다."
					);

				return false;
			}
		}

		FPaths::NormalizeDirectoryName(
			outNormalizedDirectory
		);

		FPaths::CollapseRelativeDirectories(
			outNormalizedDirectory
		);

		return true;
	}

	/**
	 * JSON 파일의 실제 절대 경로를 계산한다.
	 */
	bool ResolveJsonPath(
		const FString& rootDirectory,
		const FString& jsonRelativePath,
		FString& outAbsolutePath,
		FString& outErrorMessage
	)
	{
		FString normalizedRootDirectory;

		if (!NormalizeRootDirectory(
			rootDirectory,
			normalizedRootDirectory,
			outErrorMessage
		))
		{
			return false;
		}

		FString normalizedJsonRelativePath;

		if (!NormalizeJsonRelativePath(
			jsonRelativePath,
			normalizedJsonRelativePath,
			outErrorMessage
		))
		{
			return false;
		}

		FString contentDirectory =
			FPaths::ConvertRelativePathToFull(
				FPaths::ProjectContentDir()
			);

		FPaths::NormalizeDirectoryName(
			contentDirectory
		);

		FString absoluteRootDirectory =
			FPaths::ConvertRelativePathToFull(
				FPaths::Combine(
					contentDirectory,
					normalizedRootDirectory
				)
			);

		FPaths::NormalizeDirectoryName(
			absoluteRootDirectory
		);

		outAbsolutePath =
			FPaths::ConvertRelativePathToFull(
				FPaths::Combine(
					absoluteRootDirectory,
					normalizedJsonRelativePath
				)
			);

		FPaths::NormalizeFilename(outAbsolutePath);

		FString rootDirectoryPrefix =
			absoluteRootDirectory;

		if (!rootDirectoryPrefix.EndsWith(TEXT("/")))
		{
			rootDirectoryPrefix += TEXT("/");
		}

		if (!outAbsolutePath.StartsWith(
			rootDirectoryPrefix,
			ESearchCase::IgnoreCase
		))
		{
			outErrorMessage =
				TEXT(
					"계산된 JSON 경로가 설정된 JSON 루트 "
					"디렉터리 밖을 가리킵니다."
				);

			return false;
		}

		return true;
	}

	/**
	 * JSON 파일을 읽고 문법과 최상위 형식을 검사한다.
	 */
	bool LoadJsonDocument(
		const FString& absoluteJsonPath,
		const EExpectedJsonRootType expectedRootType,
		FLoadedJsonDocument& outDocument,
		FJsonApplyResult& inOutResult
	)
	{
		if (!IFileManager::Get().FileExists(
			*absoluteJsonPath
		))
		{
			inOutResult.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::FileRead,
					EJsonApplyIssueSeverity::Error,
					absoluteJsonPath,
					inOutResult.targetAssetPath,
					TEXT("등록된 JSON 파일을 찾을 수 없습니다.")
				)
			);

			return false;
		}

		if (!FFileHelper::LoadFileToString(
			outDocument.jsonText,
			*absoluteJsonPath
		))
		{
			inOutResult.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::FileRead,
					EJsonApplyIssueSeverity::Error,
					absoluteJsonPath,
					inOutResult.targetAssetPath,
					TEXT("JSON 파일을 문자열로 읽는 데 실패했습니다.")
				)
			);

			return false;
		}

		if (outDocument.jsonText
			.TrimStartAndEnd()
			.IsEmpty())
		{
			inOutResult.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::FileRead,
					EJsonApplyIssueSeverity::Error,
					absoluteJsonPath,
					inOutResult.targetAssetPath,
					TEXT("JSON 파일이 비어 있습니다.")
				)
			);

			return false;
		}

		const TSharedRef<TJsonReader<>> jsonReader =
			TJsonReaderFactory<>::Create(
				outDocument.jsonText
			);

		if (!FJsonSerializer::Deserialize(
			jsonReader,
			outDocument.rootValue
		) || !outDocument.rootValue.IsValid())
		{
			FString readerError =
				jsonReader->GetErrorMessage();

			if (readerError.IsEmpty())
			{
				readerError =
					TEXT("알 수 없는 JSON 문법 오류");
			}

			inOutResult.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::JsonParse,
					EJsonApplyIssueSeverity::Error,
					absoluteJsonPath,
					inOutResult.targetAssetPath,
					FString::Printf(
						TEXT(
							"JSON 문법 분석에 실패했습니다. "
							"줄: %u, 문자: %u, 원인: %s"
						),
						jsonReader->GetLineNumber(),
						jsonReader->GetCharacterNumber(),
						*readerError
					)
				)
			);

			return false;
		}

		const EJson expectedType =
			expectedRootType ==
			EExpectedJsonRootType::Array
				? EJson::Array
				: EJson::Object;

		if (outDocument.rootValue->Type != expectedType)
		{
			const FString expectedTypeName =
				expectedRootType ==
				EExpectedJsonRootType::Array
					? TEXT("Array")
					: TEXT("Object");

			inOutResult.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::Structure,
					EJsonApplyIssueSeverity::Error,
					absoluteJsonPath,
					inOutResult.targetAssetPath,
					FString::Printf(
						TEXT(
							"JSON 최상위 형식이 잘못되었습니다. "
							"필요한 형식: %s"
						),
						*expectedTypeName
					)
				)
			);

			return false;
		}

		return true;
	}

	/**
	 * DataTable JSON의 Row 기본 구조를 검사한다.
	 */
	bool ValidateDataTableRoot(
		const TSharedPtr<FJsonValue>& rootValue,
		FJsonApplyResult& inOutResult
	)
	{
		const TArray<TSharedPtr<FJsonValue>>& rows =
			rootValue->AsArray();

		TSet<FString> rowNameKeys;

		for (int32 rowIndex = 0;
			rowIndex < rows.Num();
			++rowIndex)
		{
			const TSharedPtr<FJsonValue>& rowValue =
				rows[rowIndex];

			if (!rowValue.IsValid() ||
				rowValue->Type != EJson::Object)
			{
				inOutResult.issues.Add(
					MakeIssue(
						EJsonApplyIssueStage::Structure,
						EJsonApplyIssueSeverity::Error,
						inOutResult.sourceJsonPath,
						inOutResult.targetAssetPath,
						FString::Printf(
							TEXT(
								"DataTable JSON의 %d번째 항목이 "
								"Object가 아닙니다."
							),
							rowIndex
						),
						NAME_None,
						FString::Printf(
							TEXT("[%d]"),
							rowIndex
						)
					)
				);

				continue;
			}

			const TSharedPtr<FJsonObject> rowObject =
				rowValue->AsObject();

			const TSharedPtr<FJsonValue>* nameField =
				rowObject->Values.Find(TEXT("Name"));

			if (nameField == nullptr ||
				!(*nameField).IsValid() ||
				(*nameField)->Type != EJson::String)
			{
				inOutResult.issues.Add(
					MakeIssue(
						EJsonApplyIssueStage::Structure,
						EJsonApplyIssueSeverity::Error,
						inOutResult.sourceJsonPath,
						inOutResult.targetAssetPath,
						FString::Printf(
							TEXT(
								"DataTable JSON의 %d번째 Row에 "
								"문자열 형식의 Name 필드가 없습니다."
							),
							rowIndex
						),
						NAME_None,
						FString::Printf(
							TEXT("[%d].Name"),
							rowIndex
						)
					)
				);

				continue;
			}

			const FString rowNameString =
				(*nameField)
				->AsString()
				.TrimStartAndEnd();

			if (rowNameString.IsEmpty())
			{
				inOutResult.issues.Add(
					MakeIssue(
						EJsonApplyIssueStage::Structure,
						EJsonApplyIssueSeverity::Error,
						inOutResult.sourceJsonPath,
						inOutResult.targetAssetPath,
						TEXT("DataTable Row의 Name 값이 비어 있습니다."),
						NAME_None,
						FString::Printf(
							TEXT("[%d].Name"),
							rowIndex
						)
					)
				);

				continue;
			}

			FString rowNameKey = rowNameString;
			rowNameKey.ToLowerInline();

			if (rowNameKeys.Contains(rowNameKey))
			{
				inOutResult.issues.Add(
					MakeIssue(
						EJsonApplyIssueStage::Structure,
						EJsonApplyIssueSeverity::Error,
						inOutResult.sourceJsonPath,
						inOutResult.targetAssetPath,
						FString::Printf(
							TEXT(
								"중복된 DataTable Row Name이 "
								"발견됐습니다: %s"
							),
							*rowNameString
						),
						FName(*rowNameString),
						TEXT("Name")
					)
				);

				continue;
			}

			rowNameKeys.Add(rowNameKey);
		}

		return !HasError(inOutResult);
	}

	/**
	 * Registry Binding 하나를 검사하거나 적용한다.
	 */
	FJsonApplyResult ProcessBinding(
		const EJsonApplyTargetType targetType,
		const FString& rootDirectory,
		const FString& jsonRelativePath,
		UObject* targetAsset,
		const bool applyChanges,
		const bool saveAppliedAsset,
		const bool strictValidation,
		TSet<FString>& inOutSeenJsonPaths,
		TSet<FString>& inOutSeenTargetAssets
	)
	{
		FJsonApplyResult result;

		result.targetType = targetType;
		result.sourceJsonPath = jsonRelativePath;

		if (IsValid(targetAsset))
		{
			result.targetAssetPath =
				targetAsset->GetPathName();
		}

		FString normalizedJsonRelativePath;
		FString pathErrorMessage;

		if (!NormalizeJsonRelativePath(
			jsonRelativePath,
			normalizedJsonRelativePath,
			pathErrorMessage
		))
		{
			result.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::Path,
					EJsonApplyIssueSeverity::Error,
					jsonRelativePath,
					result.targetAssetPath,
					pathErrorMessage
				)
			);
		}
		else
		{
			FString jsonPathKey =
				normalizedJsonRelativePath;

			jsonPathKey.ToLowerInline();

			if (inOutSeenJsonPaths.Contains(jsonPathKey))
			{
				result.issues.Add(
					MakeIssue(
						EJsonApplyIssueStage::Registry,
						EJsonApplyIssueSeverity::Error,
						jsonRelativePath,
						result.targetAssetPath,
						TEXT(
							"같은 JSON 상대 경로가 Registry에 "
							"중복 등록되어 있습니다."
						)
					)
				);
			}
			else
			{
				inOutSeenJsonPaths.Add(jsonPathKey);
			}
		}

		if (!IsValid(targetAsset))
		{
			result.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::Registry,
					EJsonApplyIssueSeverity::Error,
					jsonRelativePath,
					FString(),
					TEXT("Binding에 대상 에셋이 지정되지 않았습니다.")
				)
			);
		}
		else
		{
			FString targetAssetKey =
				targetAsset->GetPathName();

			targetAssetKey.ToLowerInline();

			if (inOutSeenTargetAssets.Contains(
				targetAssetKey
			))
			{
				result.issues.Add(
					MakeIssue(
						EJsonApplyIssueStage::Registry,
						EJsonApplyIssueSeverity::Error,
						jsonRelativePath,
						result.targetAssetPath,
						TEXT(
							"같은 대상 에셋이 Registry에 "
							"두 번 이상 등록되어 있습니다."
						)
					)
				);
			}
			else
			{
				inOutSeenTargetAssets.Add(
					targetAssetKey
				);
			}
		}

		if (HasError(result))
		{
			result.isSuccess = false;
			return result;
		}

		FString absoluteJsonPath;

		if (!ResolveJsonPath(
			rootDirectory,
			normalizedJsonRelativePath,
			absoluteJsonPath,
			pathErrorMessage
		))
		{
			result.issues.Add(
				MakeIssue(
					EJsonApplyIssueStage::Path,
					EJsonApplyIssueSeverity::Error,
					jsonRelativePath,
					result.targetAssetPath,
					pathErrorMessage
				)
			);

			result.isSuccess = false;
			return result;
		}

		result.sourceJsonPath = absoluteJsonPath;

		const EExpectedJsonRootType expectedRootType =
			targetType ==
			EJsonApplyTargetType::DataTable
				? EExpectedJsonRootType::Array
				: EExpectedJsonRootType::Object;

		FLoadedJsonDocument document;

		if (!LoadJsonDocument(
			absoluteJsonPath,
			expectedRootType,
			document,
			result
		))
		{
			result.isSuccess = false;
			return result;
		}

		if (targetType ==
			EJsonApplyTargetType::DataTable)
		{
			ValidateDataTableRoot(
				document.rootValue,
				result
			);
		}

		if (HasError(result))
		{
			result.isSuccess = false;
			return result;
		}

		/*
		 * Validate All JSON은 실제 대상 메모리를 변경하지 않는다.
		 *
		 * 현재 수동 메뉴 단계가 추가되기 전까지는
		 * 기본 구조 검사 결과만 반환한다.
		 */
		if (!applyChanges)
		{
			result.isSuccess = true;
			result.wasApplied = false;
			return result;
		}

		if (targetType ==
			EJsonApplyTargetType::DataTable)
		{
			UDataTable* targetDataTable =
				Cast<UDataTable>(targetAsset);

			result.isSuccess =
				FJsonDataTableProcessor::ValidateAndApply(
					targetDataTable,
					document.jsonText,
					strictValidation,
					result
				);

#if WITH_EDITOR
			if (result.isSuccess && saveAppliedAsset)
			{
				result.isSuccess =
					SaveAppliedAsset(
						targetDataTable,
						result
					);
			}
#endif

			return result;
		}

		if (targetType ==
			EJsonApplyTargetType::DataAsset)
		{
			UDataAsset* targetDataAsset =
				Cast<UDataAsset>(targetAsset);

			result.isSuccess =
				FJsonDataAssetProcessor::ValidateAndApply(
					targetDataAsset,
					document.rootValue->AsObject(),
					strictValidation,
					result
				);

#if WITH_EDITOR
			if (result.isSuccess && saveAppliedAsset)
			{
				result.isSuccess =
					SaveAppliedAsset(
						targetDataAsset,
						result
					);
			}
#endif

			return result;
		}

		result.issues.Add(
			MakeIssue(
				EJsonApplyIssueStage::Conversion,
				EJsonApplyIssueSeverity::Error,
				result.sourceJsonPath,
				result.targetAssetPath,
				TEXT("지원하지 않는 JSON 적용 대상 형식입니다.")
			)
		);

		result.isSuccess = false;
		result.wasApplied = false;

		return result;
	}

	/**
	 * Registry 전체를 검사하거나 적용한다.
	 */
	FJsonApplySummary ProcessAll(
		const UJsonApplyRegistry* registry,
		const UJsonAssetSyncSettings* settings,
		const bool applyChanges,
		const bool allowAssetSave
	)
	{
		FJsonApplySummary summary;

		if (!IsValid(settings))
		{
			summary.globalIssues.Add(
				MakeIssue(
					EJsonApplyIssueStage::Registry,
					EJsonApplyIssueSeverity::Error,
					FString(),
					FString(),
					TEXT(
						"JSON Asset Sync 프로젝트 설정을 "
						"가져올 수 없습니다."
					)
				)
			);

			return summary;
		}

		if (!IsValid(registry))
		{
			summary.globalIssues.Add(
				MakeIssue(
					EJsonApplyIssueStage::Registry,
					EJsonApplyIssueSeverity::Error,
					FString(),
					settings->registryAsset
						.ToSoftObjectPath()
						.ToString(),
					TEXT(
						"Project Settings에 지정된 Registry "
						"에셋을 불러올 수 없습니다."
					)
				)
			);

			return summary;
		}

		summary.isSystemReady = true;

		/*
		 * Apply And Save는 Unreal Editor에서만 허용한다.
		 * 패키징 게임에서는 WITH_EDITOR가 false이므로 항상 메모리 적용만 한다.
		 */
		bool saveAppliedAssets = false;

#if WITH_EDITOR
		saveAppliedAssets =
			allowAssetSave &&
			GIsEditor &&
			applyChanges &&
			settings->editorApplyMode ==
				EJsonEditorApplyMode::ApplyAndSave;
#endif

		summary.totalCount =
			registry->dataTableBindings.Num() +
			registry->dataAssetBindings.Num();

		TSet<FString> seenDataTableJsonPaths;
		TSet<FString> seenDataAssetJsonPaths;
		TSet<FString> seenTargetAssets;

		for (const FJsonDataTableBinding& binding :
			registry->dataTableBindings)
		{
			FJsonApplyResult result =
				ProcessBinding(
					EJsonApplyTargetType::DataTable,
					settings->dataTableJsonDirectory,
					binding.jsonRelativePath,
					binding.targetDataTable.Get(),
					applyChanges,
					saveAppliedAssets,
					settings->strictValidation,
					seenDataTableJsonPaths,
					seenTargetAssets
				);

			if (result.isSuccess)
			{
				++summary.successCount;
			}
			else
			{
				++summary.failureCount;
			}

			summary.results.Add(MoveTemp(result));
		}

		for (const FJsonDataAssetBinding& binding :
			registry->dataAssetBindings)
		{
			FJsonApplyResult result =
				ProcessBinding(
					EJsonApplyTargetType::DataAsset,
					settings->dataAssetJsonDirectory,
					binding.jsonRelativePath,
					binding.targetDataAsset.Get(),
					applyChanges,
					saveAppliedAssets,
					settings->strictValidation,
					seenDataAssetJsonPaths,
					seenTargetAssets
				);

			if (result.isSuccess)
			{
				++summary.successCount;
			}
			else
			{
				++summary.failureCount;
			}

			summary.results.Add(MoveTemp(result));
		}

		return summary;
	}

	const TCHAR* GetTargetTypeText(
		const EJsonApplyTargetType targetType
	)
	{
		switch (targetType)
		{
		case EJsonApplyTargetType::DataTable:
			return TEXT("DataTable");

		case EJsonApplyTargetType::DataAsset:
			return TEXT("DataAsset");

		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* GetIssueStageText(
		const EJsonApplyIssueStage stage
	)
	{
		switch (stage)
		{
		case EJsonApplyIssueStage::Registry:
			return TEXT("Registry");

		case EJsonApplyIssueStage::Path:
			return TEXT("Path");

		case EJsonApplyIssueStage::FileRead:
			return TEXT("FileRead");

		case EJsonApplyIssueStage::JsonParse:
			return TEXT("JsonParse");

		case EJsonApplyIssueStage::Structure:
			return TEXT("Structure");

		case EJsonApplyIssueStage::Conversion:
			return TEXT("Conversion");

		case EJsonApplyIssueStage::Commit:
			return TEXT("Commit");

		case EJsonApplyIssueStage::AssetSave:
			return TEXT("AssetSave");

		default:
			return TEXT("Unknown");
		}
	}

	void WriteIssueToLog(
		const FJsonApplyIssue& issue
	)
	{
		FString contextMessage =
			FString::Printf(
				TEXT("[단계: %s] JSON: %s | 대상: %s"),
				GetIssueStageText(issue.stage),
				issue.sourceJsonPath.IsEmpty()
					? TEXT("<없음>")
					: *issue.sourceJsonPath,
				issue.targetAssetPath.IsEmpty()
					? TEXT("<없음>")
					: *issue.targetAssetPath
			);

		if (!issue.rowName.IsNone())
		{
			contextMessage += FString::Printf(
				TEXT(" | Row: %s"),
				*issue.rowName.ToString()
			);
		}

		if (!issue.propertyPath.IsEmpty())
		{
			contextMessage += FString::Printf(
				TEXT(" | Field: %s"),
				*issue.propertyPath
			);
		}

		contextMessage += FString::Printf(
			TEXT(" | 원인: %s"),
			*issue.message
		);

		switch (issue.severity)
		{
		case EJsonApplyIssueSeverity::Info:
			UE_LOG(
				LogJsonAssetSync,
				Display,
				TEXT("%s"),
				*contextMessage
			);
			break;

		case EJsonApplyIssueSeverity::Warning:
			UE_LOG(
				LogJsonAssetSync,
				Warning,
				TEXT("%s"),
				*contextMessage
			);
			break;

		case EJsonApplyIssueSeverity::Error:
		default:
			UE_LOG(
				LogJsonAssetSync,
				Error,
				TEXT("%s"),
				*contextMessage
			);
			break;
		}
	}
}

FJsonApplySummary FJsonApplyService::ValidateAll(
	const UJsonApplyRegistry* registry,
	const UJsonAssetSyncSettings* settings
)
{
	return JsonAssetSync::Private::ProcessAll(
		registry,
		settings,
		false,
		false
	);
}

FJsonApplySummary FJsonApplyService::ApplyAll(
	const UJsonApplyRegistry* registry,
	const UJsonAssetSyncSettings* settings,
	const bool allowAssetSave
)
{
	return JsonAssetSync::Private::ProcessAll(
		registry,
		settings,
		true,
		allowAssetSave
	);
}

void FJsonApplyService::WriteSummaryToLog(
	const FJsonApplySummary& summary,
	const bool logSuccessfulApplications
)
{
	using namespace JsonAssetSync::Private;

	for (const FJsonApplyIssue& globalIssue :
		summary.globalIssues)
	{
		WriteIssueToLog(globalIssue);
	}

	for (const FJsonApplyResult& result :
		summary.results)
	{
		if (result.isSuccess &&
			logSuccessfulApplications)
		{
			const TCHAR* successType =
				result.wasSaved
					? TEXT("적용 및 저장 성공")
					: result.wasApplied
						? TEXT("적용 성공")
						: TEXT("검사 성공");

			UE_LOG(
				LogJsonAssetSync,
				Display,
				TEXT("[%s][%s] JSON: %s | 대상: %s"),
				successType,
				GetTargetTypeText(result.targetType),
				*result.sourceJsonPath,
				*result.targetAssetPath
			);
		}

		for (const FJsonApplyIssue& issue :
			result.issues)
		{
			WriteIssueToLog(issue);
		}
	}

	UE_LOG(
		LogJsonAssetSync,
		Display,
		TEXT(
			"JSON 처리 완료 | 시스템 준비: %s | "
			"전체: %d | 성공: %d | 실패: %d"
		),
		summary.isSystemReady
			? TEXT("True")
			: TEXT("False"),
		summary.totalCount,
		summary.successCount,
		summary.failureCount
	);
}
