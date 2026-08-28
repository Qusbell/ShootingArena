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
            "DeveloperSettings"
        });

        PrivateDependencyModuleNames.AddRange(new string[] { });

        // Editor에서만 사용하는 기능입니다.
        // Shipping/패키징 빌드에는 UnrealEd와 GameplayValidatorEditor가 포함되지 않습니다.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",
                "GameplayValidatorEditor"
            });
        }

        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
