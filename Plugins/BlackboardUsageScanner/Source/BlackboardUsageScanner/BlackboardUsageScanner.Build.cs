using UnrealBuildTool;

public class BlackboardUsageScanner : ModuleRules
{
    public BlackboardUsageScanner(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "AIModule",
                "AssetRegistry",
                "UnrealEd"
            }
        );
    }
}
