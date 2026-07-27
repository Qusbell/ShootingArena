// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class JsonAssetSyncEditor : ModuleRules
{
	public JsonAssetSyncEditor(
		ReadOnlyTargetRules Target
	) : base(Target)
	{
		/*
		 * 각 C++ 파일이 실제로 사용하는 헤더를
		 * 직접 포함하도록 설정한다.
		 */
		PCHUsage =
			PCHUsageMode.UseExplicitOrSharedPCHs;

		/*
		 * JsonAssetSyncEditor의 Public 헤더에서
		 * 직접 사용하는 모듈들이다.
		 */
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",

				/*
				 * FJsonApplyIssue, FJsonApplySummary와
				 * Runtime JSON 처리 API를 사용한다.
				 */
				"JsonAssetSync"
			}
		);

		/*
		 * JsonAssetSyncEditor 모듈의 Private CPP 구현에서만
		 * 직접 사용하는 모듈들이다.
		 */
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"DeveloperSettings",

				/*
				 * UProjectPackagingSettings 접근에 필요하다.
				 */
				"DeveloperToolSettings",

				"UnrealEd",
				"Slate",
				"SlateCore",
				"MessageLog",

				/*
				 * 에디터 상단 Tools 메뉴에
				 * Apply JSON 항목을 추가하는 데 필요하다.
				 */
				"ToolMenus"
			}
		);
	}
}
