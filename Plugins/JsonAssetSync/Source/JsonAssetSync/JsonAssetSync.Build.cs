// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class JsonAssetSync : ModuleRules
{
    public JsonAssetSync(ReadOnlyTargetRules Target) : base(Target)
    {
        // 각 C++ 파일이 자신에게 필요한 헤더를 직접 포함하도록 한다.
        // 의존 관계가 명확해지고 대규모 프로젝트에서도 관리하기 편하다.
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Public 헤더에서 직접 사용하는 모듈들이다.
        // 다른 모듈이 JsonAssetSync의 Public 헤더를 포함할 때도
        // 아래 타입들을 확인할 수 있어야 한다.
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",

				// UDeveloperSettings를 사용하여
				// Project Settings에 플러그인 설정을 노출한다.
				"DeveloperSettings"
            }
        );

        // JsonAssetSync 모듈 내부 구현에서만 사용하는 모듈들이다.
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
				// JSON 문법 분석과 FJsonObject/FJsonValue 사용에 필요하다.
				"Json",

				// JSON과 Unreal Struct 또는 UObject Property 사이의
				// 범용 변환 기능을 사용할 때 필요하다.
				"JsonUtilities"
            }
        );
    }
}