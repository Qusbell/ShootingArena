// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class ShootingArena : ModuleRules
{
	public ShootingArena(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"NavigationSystem",
			"AIModule",
			"DeveloperSettings",
            "Slate",
			"SlateCore"
        } );

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// PathLink의 Editor 전용 중복 배치 가드레일에서 PIE를 즉시 종료하기 위해 사용합니다.
		// 패키징 빌드에는 UnrealEd가 포함되지 않도록 Editor Target에서만 의존성을 추가합니다.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
