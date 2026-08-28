// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class ShootingArena : ModuleRules
{
    public ShootingArena(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "NavigationSystem",
            "AIModule",
            "DeveloperSettings",
            "Slate",
            "SlateCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[] { });

        // Editor 전용 기능입니다.
        // 패키징/Shipping 빌드에는 UnrealEd와 GameplayValidatorEditor가 포함되지 않습니다.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",
                "GameplayValidatorEditor"
            });
        }

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file
        // with the Enabled attribute set to true
    }
}