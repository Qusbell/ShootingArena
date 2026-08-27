// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonAssetSyncPackagingPreflight.h"

#include "JsonApplyRegistry.h"
#include "JsonApplyService.h"
#include "JsonAssetSyncSettings.h"

#include "Engine/DataAsset.h"
#include "Engine/CurveTable.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Settings/ProjectPackagingSettings.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace JsonAssetSync::PackagingPreflightPrivate
{
	/**
	 * 패키징 사전검사 문제를 추가한다.
	 */
	void AddIssue(
		FJsonAssetSyncPackagingPreflightReport& inOutReport,
		const EJsonApplyIssueSeverity severity,
		const FString& message,
		const FString& sourcePath = FString(),
		const FString& targetPath = FString()
	)
	{
		FJsonApplyIssue issue;

		issue.stage = EJsonApplyIssueStage::Registry;
		issue.severity = severity;
		issue.sourceJsonPath = sourcePath;
		issue.targetAssetPath = targetPath;
		issue.message = message;

		inOutReport.issues.Add(MoveTemp(issue));
	}

	/**
	 * Content 폴더 기준 상대 디렉터리를 검사하고 정규화한다.
	 */
	bool NormalizeRelativeContentDirectory(
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

		while (outNormalizedDirectory.StartsWith(TEXT("/")))
		{
			outNormalizedDirectory.RightChopInline(1);
		}

		if (outNormalizedDirectory.IsEmpty())
		{
			outErrorMessage =
				TEXT("Content 기준 상대 디렉터리가 비어 있습니다.");

			return false;
		}

		if (!FPaths::IsRelative(outNormalizedDirectory))
		{
			outErrorMessage =
				TEXT(
					"외부 JSON 디렉터리는 프로젝트 Content 폴더 "
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
						"외부 JSON 디렉터리에 상위 폴더 이동을 "
						"의미하는 '..'를 사용할 수 없습니다."
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

		return !outNormalizedDirectory.IsEmpty();
	}

	/**
	 * 비교용 Content 상대 디렉터리 키를 만든다.
	 */
	FString MakeRelativeDirectoryKey(
		const FString& inputDirectory
	)
	{
		FString normalizedDirectory =
			inputDirectory.TrimStartAndEnd();

		normalizedDirectory.ReplaceInline(
			TEXT("\\"),
			TEXT("/")
		);

		while (normalizedDirectory.StartsWith(TEXT("/")))
		{
			normalizedDirectory.RightChopInline(1);
		}

		FPaths::NormalizeDirectoryName(
			normalizedDirectory
		);

		normalizedDirectory.ToLowerInline();

		return normalizedDirectory;
	}

	/**
	 * 비교용 Unreal Long Package Directory 키를 만든다.
	 */
	FString MakePackageDirectoryKey(
		const FString& inputDirectory
	)
	{
		FString normalizedDirectory =
			inputDirectory.TrimStartAndEnd();

		normalizedDirectory.ReplaceInline(
			TEXT("\\"),
			TEXT("/")
		);

		if (!normalizedDirectory.StartsWith(TEXT("/")))
		{
			normalizedDirectory =
				FString::Printf(
					TEXT("/Game/%s"),
					*normalizedDirectory
				);
		}

		FPaths::NormalizeDirectoryName(
			normalizedDirectory
		);

		normalizedDirectory.ToLowerInline();

		return normalizedDirectory;
	}

	/**
	 * 패키징 설정에 Content 상대 디렉터리가 존재하는지 확인한다.
	 */
	bool ContainsRelativeDirectory(
		const TArray<FDirectoryPath>& directories,
		const FString& expectedDirectory
	)
	{
		const FString expectedKey =
			MakeRelativeDirectoryKey(
				expectedDirectory
			);

		for (const FDirectoryPath& directory :
			directories)
		{
			if (MakeRelativeDirectoryKey(directory.Path) ==
				expectedKey)
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * 패키징 설정에 Long Package Directory가 존재하는지 확인한다.
	 */
	bool ContainsPackageDirectory(
		const TArray<FDirectoryPath>& directories,
		const FString& expectedDirectory
	)
	{
		const FString expectedKey =
			MakePackageDirectoryKey(
				expectedDirectory
			);

		for (const FDirectoryPath& directory :
			directories)
		{
			if (MakePackageDirectoryKey(directory.Path) ==
				expectedKey)
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * 외부 JSON 폴더 복사 설정을 확인하고 필요할 때 자동 추가한다.
	 */
	void EnsureNonUfsCopyDirectory(
		UProjectPackagingSettings* packagingSettings,
		const FString& configuredDirectory,
		const bool autoConfigure,
		FJsonAssetSyncPackagingPreflightReport& inOutReport
	)
	{
		FString normalizedDirectory;
		FString errorMessage;

		if (!NormalizeRelativeContentDirectory(
			configuredDirectory,
			normalizedDirectory,
			errorMessage
		))
		{
			AddIssue(
				inOutReport,
				EJsonApplyIssueSeverity::Error,
				errorMessage,
				configuredDirectory
			);

			return;
		}

		const FString absoluteDirectory =
			FPaths::ConvertRelativePathToFull(
				FPaths::Combine(
					FPaths::ProjectContentDir(),
					normalizedDirectory
				)
			);

		if (!IFileManager::Get().DirectoryExists(
			*absoluteDirectory
		))
		{
			if (autoConfigure &&
				IFileManager::Get().MakeDirectory(
					*absoluteDirectory,
					true
				))
			{
				AddIssue(
					inOutReport,
					EJsonApplyIssueSeverity::Info,
					FString::Printf(
						TEXT(
							"누락된 외부 JSON 디렉터리를 "
							"자동 생성했습니다: %s"
						),
						*absoluteDirectory
					),
					absoluteDirectory
				);
			}
			else
			{
				AddIssue(
					inOutReport,
					EJsonApplyIssueSeverity::Error,
					FString::Printf(
						TEXT(
							"외부 JSON 디렉터리를 찾을 수 없습니다: %s"
						),
						*absoluteDirectory
					),
					absoluteDirectory
				);
			}
		}

		if (ContainsRelativeDirectory(
			packagingSettings
				->DirectoriesToAlwaysStageAsNonUFS,
			normalizedDirectory
		))
		{
			return;
		}

		if (!autoConfigure)
		{
			AddIssue(
				inOutReport,
				EJsonApplyIssueSeverity::Error,
				FString::Printf(
					TEXT(
						"패키징 설정의 Additional Non-Asset "
						"Directories To Copy에 다음 경로가 없습니다: %s"
					),
					*normalizedDirectory
				),
				normalizedDirectory
			);

			return;
		}

		FDirectoryPath newDirectory;
		newDirectory.Path = normalizedDirectory;

		packagingSettings
			->DirectoriesToAlwaysStageAsNonUFS
			.Add(MoveTemp(newDirectory));

		inOutReport.packagingSettingsChanged = true;

		AddIssue(
			inOutReport,
			EJsonApplyIssueSeverity::Info,
			FString::Printf(
				TEXT(
					"Additional Non-Asset Directories To Copy에 "
					"경로를 자동 추가했습니다: %s"
				),
				*normalizedDirectory
			),
			normalizedDirectory
		);
	}

	/**
	 * Registry가 있는 패키지 폴더를 Always Cook 설정에 추가한다.
	 */
	void EnsureRegistryCookDirectory(
		UProjectPackagingSettings* packagingSettings,
		const UJsonAssetSyncSettings* settings,
		const bool autoConfigure,
		FJsonAssetSyncPackagingPreflightReport& inOutReport
	)
	{
		if (!IsValid(settings) ||
			settings->registryAsset.IsNull())
		{
			AddIssue(
				inOutReport,
				EJsonApplyIssueSeverity::Error,
				TEXT(
					"Project Settings에 Registry Asset이 "
					"지정되어 있지 않습니다."
				)
			);

			return;
		}

		const FSoftObjectPath registryObjectPath =
			settings->registryAsset.ToSoftObjectPath();

		const FString registryPackageName =
			registryObjectPath.GetLongPackageName();

		if (!FPackageName::IsValidLongPackageName(
			registryPackageName
		))
		{
			AddIssue(
				inOutReport,
				EJsonApplyIssueSeverity::Error,
				FString::Printf(
					TEXT(
						"Registry의 Unreal Package 경로가 "
						"유효하지 않습니다: %s"
					),
					*registryPackageName
				),
				FString(),
				registryObjectPath.ToString()
			);

			return;
		}

		const FString registryPackageDirectory =
			FPackageName::GetLongPackagePath(
				registryPackageName
			);

		if (registryPackageDirectory.IsEmpty())
		{
			AddIssue(
				inOutReport,
				EJsonApplyIssueSeverity::Error,
				TEXT(
					"Registry가 포함된 Unreal Package 폴더를 "
					"계산하지 못했습니다."
				),
				FString(),
				registryObjectPath.ToString()
			);

			return;
		}

		if (ContainsPackageDirectory(
			packagingSettings->DirectoriesToAlwaysCook,
			registryPackageDirectory
		))
		{
			return;
		}

		if (!autoConfigure)
		{
			AddIssue(
				inOutReport,
				EJsonApplyIssueSeverity::Error,
				FString::Printf(
					TEXT(
						"패키징 설정의 Additional Asset "
						"Directories to Cook에 Registry 폴더가 "
						"없습니다: %s"
					),
					*registryPackageDirectory
				),
				FString(),
				registryObjectPath.ToString()
			);

			return;
		}

		FDirectoryPath newDirectory;
		newDirectory.Path = registryPackageDirectory;

		packagingSettings
			->DirectoriesToAlwaysCook
			.Add(MoveTemp(newDirectory));

		inOutReport.packagingSettingsChanged = true;

		AddIssue(
			inOutReport,
			EJsonApplyIssueSeverity::Info,
			FString::Printf(
				TEXT(
					"Additional Asset Directories to Cook에 "
					"Registry 폴더를 자동 추가했습니다: %s"
				),
				*registryPackageDirectory
			),
			FString(),
			registryObjectPath.ToString()
		);
	}

	/**
	 * Registry Binding의 대상 에셋이 유효한지 확인한다.
	 */
	void ValidateRegistryBindings(
		const UJsonApplyRegistry* registry,
		FJsonAssetSyncPackagingPreflightReport& inOutReport
	)
	{
		if (!IsValid(registry))
		{
			return;
		}

		for (int32 bindingIndex = 0;
			bindingIndex < registry->dataTableBindings.Num();
			++bindingIndex)
		{
			const FJsonDataTableBinding& binding =
				registry->dataTableBindings[bindingIndex];

			if (!IsValid(binding.targetDataTable))
			{
				AddIssue(
					inOutReport,
					EJsonApplyIssueSeverity::Error,
					FString::Printf(
						TEXT(
							"DataTable Binding %d에 대상 "
							"DataTable이 지정되어 있지 않습니다."
						),
						bindingIndex
					),
					binding.jsonRelativePath
				);
			}
		}


		for (int32 bindingIndex = 0;
			bindingIndex < registry->curveTableBindings.Num();
			++bindingIndex)
		{
			const FJsonCurveTableBinding& binding =
				registry->curveTableBindings[bindingIndex];

			if (!IsValid(binding.targetCurveTable))
			{
				AddIssue(
					inOutReport,
					EJsonApplyIssueSeverity::Error,
					FString::Printf(
						TEXT(
							"CurveTable Binding %d에 대상 "
							"CurveTable이 지정되어 있지 않습니다."
						),
						bindingIndex
					),
					binding.jsonRelativePath
				);
			}
		}


		for (int32 bindingIndex = 0;
			bindingIndex < registry->floatCurveBindings.Num();
			++bindingIndex)
		{
			const FJsonFloatCurveBinding& binding =
				registry->floatCurveBindings[bindingIndex];

			if (!IsValid(binding.targetFloatCurve))
			{
				AddIssue(
					inOutReport,
					EJsonApplyIssueSeverity::Error,
					FString::Printf(
						TEXT(
							"FloatCurve Binding %d에 대상 "
							"Curve Float가 지정되어 있지 않습니다."
						),
						bindingIndex
					),
					binding.jsonRelativePath
				);
			}
		}

		for (int32 bindingIndex = 0;
			bindingIndex < registry->dataAssetBindings.Num();
			++bindingIndex)
		{
			const FJsonDataAssetBinding& binding =
				registry->dataAssetBindings[bindingIndex];

			if (!IsValid(binding.targetDataAsset))
			{
				AddIssue(
					inOutReport,
					EJsonApplyIssueSeverity::Error,
					FString::Printf(
						TEXT(
							"DataAsset Binding %d에 대상 "
							"DataAsset이 지정되어 있지 않습니다."
						),
						bindingIndex
					),
					binding.jsonRelativePath
				);
			}
		}
	}

	/**
	 * Registry와 모든 대상 에셋을 임시 객체로 복제한다.
	 *
	 * 이후 ApplyAll을 호출해도 실제 프로젝트 에셋은 변경되지 않는다.
	 */
	UJsonApplyRegistry* CreateTransientRegistryCopy(
		const UJsonApplyRegistry* sourceRegistry,
		FJsonAssetSyncPackagingPreflightReport& inOutReport
	)
	{
		if (!IsValid(sourceRegistry))
		{
			return nullptr;
		}

		UJsonApplyRegistry* transientRegistry =
			DuplicateObject<UJsonApplyRegistry>(
				sourceRegistry,
				GetTransientPackage(),
				NAME_None
			);

		if (!IsValid(transientRegistry))
		{
			AddIssue(
				inOutReport,
				EJsonApplyIssueSeverity::Error,
				TEXT(
					"패키징 Dry Run용 임시 Registry를 "
					"복제하지 못했습니다."
				)
			);

			return nullptr;
		}

		transientRegistry->SetFlags(RF_Transient);

		for (FJsonDataTableBinding& binding :
			transientRegistry->dataTableBindings)
		{
			UDataTable* sourceDataTable =
				binding.targetDataTable.Get();

			if (!IsValid(sourceDataTable))
			{
				continue;
			}

			const FName duplicateName =
				MakeUniqueObjectName(
					transientRegistry,
					sourceDataTable->GetClass(),
					sourceDataTable->GetFName()
				);

			UDataTable* transientDataTable =
				DuplicateObject<UDataTable>(
					sourceDataTable,
					transientRegistry,
					duplicateName
				);

			if (!IsValid(transientDataTable))
			{
				AddIssue(
					inOutReport,
					EJsonApplyIssueSeverity::Error,
					TEXT(
						"패키징 Dry Run용 임시 DataTable을 "
						"복제하지 못했습니다."
					),
					binding.jsonRelativePath,
					sourceDataTable->GetPathName()
				);

				continue;
			}

			transientDataTable->SetFlags(RF_Transient);
			binding.targetDataTable = transientDataTable;
		}


		for (FJsonCurveTableBinding& binding :
			transientRegistry->curveTableBindings)
		{
			UCurveTable* sourceCurveTable =
				binding.targetCurveTable.Get();

			if (!IsValid(sourceCurveTable))
			{
				continue;
			}

			const FName duplicateName =
				MakeUniqueObjectName(
					transientRegistry,
					sourceCurveTable->GetClass(),
					sourceCurveTable->GetFName()
				);

			UCurveTable* transientCurveTable =
				DuplicateObject<UCurveTable>(
					sourceCurveTable,
					transientRegistry,
					duplicateName
				);

			if (!IsValid(transientCurveTable))
			{
				AddIssue(
					inOutReport,
					EJsonApplyIssueSeverity::Error,
					TEXT(
						"패키징 Dry Run용 임시 CurveTable을 "
						"복제하지 못했습니다."
					),
					binding.jsonRelativePath,
					sourceCurveTable->GetPathName()
				);

				continue;
			}

			transientCurveTable->SetFlags(RF_Transient);
			binding.targetCurveTable = transientCurveTable;
		}


		for (FJsonFloatCurveBinding& binding :
			transientRegistry->floatCurveBindings)
		{
			UCurveFloat* sourceFloatCurve =
				binding.targetFloatCurve.Get();

			if (!IsValid(sourceFloatCurve))
			{
				continue;
			}

			const FName duplicateName =
				MakeUniqueObjectName(
					transientRegistry,
					sourceFloatCurve->GetClass(),
					sourceFloatCurve->GetFName()
				);

			UCurveFloat* transientFloatCurve =
				DuplicateObject<UCurveFloat>(
					sourceFloatCurve,
					transientRegistry,
					duplicateName
				);

			if (!IsValid(transientFloatCurve))
			{
				AddIssue(
					inOutReport,
					EJsonApplyIssueSeverity::Error,
					TEXT(
						"패키징 Dry Run용 임시 Curve Float를 "
						"복제하지 못했습니다."
					),
					binding.jsonRelativePath,
					sourceFloatCurve->GetPathName()
				);

				continue;
			}

			transientFloatCurve->SetFlags(RF_Transient);
			binding.targetFloatCurve = transientFloatCurve;
		}

		for (FJsonDataAssetBinding& binding :
			transientRegistry->dataAssetBindings)
		{
			UDataAsset* sourceDataAsset =
				binding.targetDataAsset.Get();

			if (!IsValid(sourceDataAsset))
			{
				continue;
			}

			const FName duplicateName =
				MakeUniqueObjectName(
					transientRegistry,
					sourceDataAsset->GetClass(),
					sourceDataAsset->GetFName()
				);

			UDataAsset* transientDataAsset =
				DuplicateObject<UDataAsset>(
					sourceDataAsset,
					transientRegistry,
					duplicateName
				);

			if (!IsValid(transientDataAsset))
			{
				AddIssue(
					inOutReport,
					EJsonApplyIssueSeverity::Error,
					TEXT(
						"패키징 Dry Run용 임시 DataAsset을 "
						"복제하지 못했습니다."
					),
					binding.jsonRelativePath,
					sourceDataAsset->GetPathName()
				);

				continue;
			}

			transientDataAsset->SetFlags(RF_Transient);
			binding.targetDataAsset = transientDataAsset;
		}

		return transientRegistry;
	}

	/**
	 * 사전검사 결과에 Error가 존재하는지 확인한다.
	 */
	bool HasPreflightError(
		const FJsonAssetSyncPackagingPreflightReport& report
	)
	{
		for (const FJsonApplyIssue& issue : report.issues)
		{
			if (issue.severity ==
				EJsonApplyIssueSeverity::Error)
			{
				return true;
			}
		}

		return false;
	}
}

FJsonAssetSyncPackagingPreflightReport
FJsonAssetSyncPackagingPreflight::Run(
	const bool autoConfigurePackagingSettings
)
{
	using namespace JsonAssetSync::PackagingPreflightPrivate;

	FJsonAssetSyncPackagingPreflightReport report;

	const UJsonAssetSyncSettings* settings =
		GetDefault<UJsonAssetSyncSettings>();

	if (!IsValid(settings))
	{
		AddIssue(
			report,
			EJsonApplyIssueSeverity::Error,
			TEXT(
				"JSON Asset Sync Project Settings를 "
				"가져올 수 없습니다."
			)
		);

		return report;
	}

	if (!settings->applyOnRuntimeStartup)
	{
		AddIssue(
			report,
			EJsonApplyIssueSeverity::Error,
			TEXT(
				"Apply On Runtime Startup이 꺼져 있습니다. "
				"패키징 게임 시작 시 JSON이 자동 적용되지 않습니다."
			)
		);
	}

	UProjectPackagingSettings* packagingSettings =
		GetMutableDefault<UProjectPackagingSettings>();

	if (!IsValid(packagingSettings))
	{
		AddIssue(
			report,
			EJsonApplyIssueSeverity::Error,
			TEXT(
				"Unreal Project Packaging Settings를 "
				"가져올 수 없습니다."
			)
		);

		return report;
	}

	EnsureNonUfsCopyDirectory(
		packagingSettings,
		settings->dataTableJsonDirectory,
		autoConfigurePackagingSettings,
		report
	);

	EnsureNonUfsCopyDirectory(
		packagingSettings,
		settings->curveTableJsonDirectory,
		autoConfigurePackagingSettings,
		report
	);

	EnsureNonUfsCopyDirectory(
		packagingSettings,
		settings->floatCurveJsonDirectory,
		autoConfigurePackagingSettings,
		report
	);

	EnsureNonUfsCopyDirectory(
		packagingSettings,
		settings->dataAssetJsonDirectory,
		autoConfigurePackagingSettings,
		report
	);

	EnsureRegistryCookDirectory(
		packagingSettings,
		settings,
		autoConfigurePackagingSettings,
		report
	);

	/*
	 * 패키징 설정이 자동 변경됐다면 DefaultGame.ini에 저장한다.
	 */
	if (report.packagingSettingsChanged)
	{
		packagingSettings->PostEditChange();

		const bool configSaved =
			packagingSettings
				->TryUpdateDefaultConfigFile(
					FString(),
					true
				);

		if (!configSaved)
		{
			AddIssue(
				report,
				EJsonApplyIssueSeverity::Error,
				TEXT(
					"자동으로 보완한 패키징 설정을 "
					"DefaultGame.ini에 저장하지 못했습니다."
				)
			);
		}
	}

	UJsonApplyRegistry* registry = nullptr;

	if (!settings->registryAsset.IsNull())
	{
		registry =
			settings->registryAsset.LoadSynchronous();
	}

	if (!IsValid(registry))
	{
		AddIssue(
			report,
			EJsonApplyIssueSeverity::Error,
			TEXT(
				"Project Settings에 지정된 Registry Asset을 "
				"로드하지 못했습니다."
			),
			FString(),
			settings->registryAsset
				.ToSoftObjectPath()
				.ToString()
		);

		return report;
	}

	ValidateRegistryBindings(
		registry,
		report
	);

	/*
	 * 실제 에셋의 복제본에 ApplyAll을 실행한다.
	 *
	 * 파일 존재, JSON 문법, Row/필드 구조뿐 아니라
	 * DataTable RowStruct 및 DataAsset UPROPERTY 타입 변환과
	 * Commit 로직까지 검사한다.
	 *
	 * 이 Registry와 대상 에셋은 Transient 복제본이므로
	 * Editor Apply Mode가 Apply And Save여도 디스크 저장은 금지한다.
	 */
	UJsonApplyRegistry* transientRegistry =
		CreateTransientRegistryCopy(
			registry,
			report
		);

	if (IsValid(transientRegistry))
	{
		report.dryRunSummary =
			FJsonApplyService::ApplyAll(
				transientRegistry,
				settings,
				false // Dry Run 복제본은 메모리 적용만 하고 저장하지 않는다.
			);
	}

	report.isReady =
		!HasPreflightError(report) &&
		report.dryRunSummary.isSystemReady &&
		report.dryRunSummary.failureCount == 0;

	return report;
}
