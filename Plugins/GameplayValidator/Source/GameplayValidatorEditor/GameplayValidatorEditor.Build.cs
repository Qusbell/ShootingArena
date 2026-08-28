using UnrealBuildTool;

public class GameplayValidatorEditor : ModuleRules
{
    public GameplayValidatorEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "EditorSubsystem"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Slate",
                "SlateCore",
                "UnrealEd",
                "LevelEditor",
                "ToolMenus",
                "InputCore",
                "PropertyEditor",
                "Projects",
                "NavigationSystem"
            });
    }
}
